/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkOpenGLES30PolyDataMapper2D.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkOpenGLES30PolyDataMapper2D.h"
#include "vtkActor2D.h"
#include "vtkArrayDispatch.txx"
#include "vtkDataArray.h"
#include "vtkDataArrayRange.h"
#include "vtkHardwareSelector.h"
#include "vtkInformation.h"
#include "vtkLogger.h"
#include "vtkObjectFactory.h"
#include "vtkOpenGLCellToVTKCellMap.h"
#include "vtkOpenGLError.h"
#include "vtkOpenGLHelper.h"
#include "vtkOpenGLIndexBufferObject.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGLResourceFreeCallback.h"
#include "vtkOpenGLState.h"
#include "vtkOpenGLVertexArrayObject.h"
#include "vtkOpenGLVertexBufferObjectGroup.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkPolyData2DFS.h"
#include "vtkPolyData2DVS.h"
#include "vtkProperty.h"
#include "vtkProperty2D.h"
#include "vtkRenderer.h"
#include "vtkShaderProgram.h"
#include "vtkSmartPointer.h"
#include "vtkTextureObject.h"
#include "vtkUnsignedCharArray.h"
#include "vtkViewport.h"

namespace
{

template <typename T>
class ScopedValueRollback
{
public:
  ScopedValueRollback(T& value, T newValue)
  {
    Value = value;
    Pointer = &value;
    *Pointer = newValue;
  }
  ~ScopedValueRollback() { *Pointer = Value; }

private:
  T* Pointer = nullptr;
  T Value;
};

struct VertexAttributeArrays
{
  vtkSmartPointer<vtkDataArray> colors;
  vtkSmartPointer<vtkDataArray> points;
  vtkSmartPointer<vtkDataArray> tcoords;

  void operator=(VertexAttributeArrays& other)
  {
    if (other.colors != nullptr)
    {
      this->colors = vtk::TakeSmartPointer(other.colors->NewInstance());
      this->colors->SetNumberOfComponents(other.colors->GetNumberOfComponents());
    }
    if (other.points != nullptr)
    {
      this->points = vtk::TakeSmartPointer(other.points->NewInstance());
      this->points->SetNumberOfComponents(other.points->GetNumberOfComponents());
    }
    if (other.tcoords != nullptr)
    {
      this->tcoords = vtk::TakeSmartPointer(other.tcoords->NewInstance());
      this->tcoords->SetNumberOfComponents(other.tcoords->GetNumberOfComponents());
    }
  }

  void Resize(int npts)
  {
    if (this->colors != nullptr)
    {
      this->colors->SetNumberOfTuples(npts);
    }
    if (this->points != nullptr)
    {
      this->points->SetNumberOfTuples(npts);
    }
    if (this->tcoords != nullptr)
    {
      this->tcoords->SetNumberOfTuples(npts);
    }
  }
};

struct vtkExpandVertexAttributes
{
  template <typename T1, typename T2>
  void operator()(T1* src, T2* dst, const unsigned int* indices, const std::size_t numIndices)
  {
    auto srcRange = vtk::DataArrayTupleRange(src);
    auto dstRange = vtk::DataArrayTupleRange(dst);
    const int numComponents = src->GetNumberOfComponents();
    if (numComponents != dst->GetNumberOfComponents())
    {
      vtkLog(ERROR, << __func__ << ": Mismatch in source and destination components.");
    }
    int dstPtId = 0;
    for (std::size_t i = 0; i < numIndices; ++i)
    {
      const auto& ptId = indices[i];
      for (int comp = 0; comp < numComponents; ++comp)
      {
        dstRange[dstPtId][comp] = srcRange[ptId][comp];
      }
      ++dstPtId;
    }
  }
};
}

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtkOpenGLES30PolyDataMapper2D);

//------------------------------------------------------------------------------
vtkOpenGLES30PolyDataMapper2D::vtkOpenGLES30PolyDataMapper2D() = default;

//------------------------------------------------------------------------------
vtkOpenGLES30PolyDataMapper2D::~vtkOpenGLES30PolyDataMapper2D() = default;

//------------------------------------------------------------------------------
void vtkOpenGLES30PolyDataMapper2D::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkOpenGLES30PolyDataMapper2D::RenderOverlay(vtkViewport* viewport, vtkActor2D* actor)
{
  vtkOpenGLClearErrorMacro();
  vtkPolyData* input = this->GetInput();

  vtkDebugMacro(<< "vtkOpenGLPolyDataMapper2D::Render");

  if (input == nullptr)
  {
    vtkErrorMacro(<< "No input!");
    return;
  }

  this->GetInputAlgorithm()->Update();
  vtkIdType numPts = input->GetNumberOfPoints();
  if (numPts == 0)
  {
    vtkDebugMacro(<< "No points!");
    return;
  }

  if (this->LookupTable == nullptr)
  {
    this->CreateDefaultLookupTable();
  }

  vtkOpenGLRenderWindow* renWin = static_cast<vtkOpenGLRenderWindow*>(viewport->GetVTKWindow());
  vtkOpenGLState* ostate = renWin->GetState();

  this->ResourceCallback->RegisterGraphicsResources(renWin);

  vtkRenderer* ren = vtkRenderer::SafeDownCast(viewport);
  vtkHardwareSelector* selector = ren->GetSelector();
  if (selector)
  {
    selector->BeginRenderProp();
  }

  int picking = (selector ? 1 : 0);
  if (picking != this->LastPickState)
  {
    this->LastPickState = picking;
    this->PickStateChanged.Modified();
  }

  // Assume we want to do Zbuffering for now.
  // we may turn this off later
  ostate->vtkglDepthMask(GL_TRUE);

  // Update the VBO if needed.
  if (this->VBOUpdateTime < this->GetMTime() || this->VBOUpdateTime < actor->GetMTime() ||
    this->VBOUpdateTime < input->GetMTime() ||
    (this->TransformCoordinate &&
      (this->VBOUpdateTime < viewport->GetMTime() ||
        this->VBOUpdateTime < viewport->GetVTKWindow()->GetMTime())))
  {
    this->UpdateVBO(actor, viewport);
    this->VBOUpdateTime.Modified();
  }

  this->LastBoundBO = nullptr;

  // Figure out and build the appropriate shader for the mapped geometry.
  this->PrimitiveIDOffset = 0;

  vtkOpenGLHelper* cellBOs[PrimitiveEnd] = { &this->Points, &this->Lines, &this->Tris,
    &this->TriStrips };
  const GLenum modes[PrimitiveEnd] = { GL_POINTS, GL_LINES, GL_TRIANGLES, GL_TRIANGLES };
  for (int primType = 0; primType < PrimitiveEnd; ++primType)
  {
    const auto numVerts = this->PrimitiveIndexArrays[primType].size();
    ScopedValueRollback<vtkOpenGLVertexBufferObjectGroup*> vbogBkp(
      this->VBOs, this->PrimitiveVBOGroup[primType].Get());
    this->CurrentDrawCallPrimtiveType = static_cast<PrimitiveTypes>(primType);
    this->UpdateShaders(*cellBOs[primType], ren, actor);
    glDrawArrays(modes[primType], 0, numVerts);
  }

  if (this->LastBoundBO)
  {
    this->LastBoundBO->VAO->Release();
  }

  if (selector)
  {
    selector->EndRenderProp();
  }

  vtkOpenGLCheckErrorMacro("failed after RenderOverlay");
}

//------------------------------------------------------------------------------
void vtkOpenGLES30PolyDataMapper2D::ReleaseGraphicsResources(vtkWindow* win)
{
  if (!this->ResourceCallback->IsReleasing())
  {
    this->ResourceCallback->Release();
    return;
  }
  for (int i = PrimitiveStart; i < PrimitiveEnd; i++)
  {
    this->PrimitiveVBOGroup[i]->ReleaseGraphicsResources(win);
  }
  this->Superclass::ReleaseGraphicsResources(win);
}

//------------------------------------------------------------------------------
void vtkOpenGLES30PolyDataMapper2D::BuildShaders(std::string& VSSource, std::string& FSSource,
  std::string& GSSource, vtkViewport* viewport, vtkActor2D* actor)
{
  // false so that superclass uses point color vertex attribute.
  ScopedValueRollback<bool> cellScalarsBkp(this->HaveCellScalars, false);
  this->Superclass::BuildShaders(VSSource, FSSource, GSSource, viewport, actor);
  GSSource.clear();
  if (this->CurrentDrawCallPrimtiveType == PrimitivePoints)
  {
    this->ReplaceShaderPointSize(VSSource, viewport, actor);
  }
}

//------------------------------------------------------------------------------
void vtkOpenGLES30PolyDataMapper2D::ReplaceShaderPointSize(
  std::string& VSSource, vtkViewport* vtkNotUsed(viewport), vtkActor2D* vtkNotUsed(act))
{
  vtkShaderProgram::Substitute(VSSource, "//VTK::PointSizeGLES30::Dec", "uniform float PointSize;");
  vtkShaderProgram::Substitute(
    VSSource, "//VTK::PointSizeGLES30::Impl", "gl_PointSize = PointSize;");
}

//------------------------------------------------------------------------------
void vtkOpenGLES30PolyDataMapper2D::UpdateShaders(
  vtkOpenGLHelper& cellBO, vtkViewport* viewport, vtkActor2D* act)
{
  this->Superclass::UpdateShaders(cellBO, viewport, act);
}

//------------------------------------------------------------------------------
void vtkOpenGLES30PolyDataMapper2D::SetMapperShaderParameters(
  vtkOpenGLHelper& cellBO, vtkViewport* viewport, vtkActor2D* act)
{
  // false so that superclass doesn't try to fetch cell scalar texture. we don't do that here.
  ScopedValueRollback<bool> cellScalarsBkp(this->HaveCellScalars, false);
  this->Superclass::SetMapperShaderParameters(cellBO, viewport, act);
  if (this->CurrentDrawCallPrimtiveType == PrimitivePoints &&
    cellBO.Program->IsUniformUsed("PointSize"))
  {
    cellBO.Program->SetUniformf("PointSize", act->GetProperty()->GetPointSize());
  }
  vtkOpenGLCheckErrorMacro("failed after UpdateShader PointSize ");
}

//------------------------------------------------------------------------------
void vtkOpenGLES30PolyDataMapper2D::UpdateVBO(vtkActor2D* act, vtkViewport* viewport)
{
  vtkPolyData* poly = this->GetInput();
  if (poly == nullptr)
  {
    return;
  }

  this->MapScalars(act->GetProperty()->GetOpacity());
  this->HaveCellScalars = false;
  if (this->ScalarVisibility)
  {
    // We must figure out how the scalars should be mapped to the polydata.
    if ((this->ScalarMode == VTK_SCALAR_MODE_USE_CELL_DATA ||
          this->ScalarMode == VTK_SCALAR_MODE_USE_CELL_FIELD_DATA ||
          this->ScalarMode == VTK_SCALAR_MODE_USE_FIELD_DATA ||
          !poly->GetPointData()->GetScalars()) &&
      this->ScalarMode != VTK_SCALAR_MODE_USE_POINT_FIELD_DATA && this->Colors)
    {
      this->HaveCellScalars = true;
    }
  }

  // if we have cell scalars then we have to
  // build the texture
  vtkCellArray* prims[4];
  prims[0] = poly->GetVerts();
  prims[1] = poly->GetLines();
  prims[2] = poly->GetPolys();
  prims[3] = poly->GetStrips();
  vtkDataArray* c = this->Colors;
  if (this->HaveCellScalars)
  {
    this->CellCellMap->Update(prims, VTK_SURFACE, poly->GetPoints());
    c = nullptr;
  }

  // do we have texture maps?
  bool haveTextures = false;
  vtkInformation* info = act->GetPropertyKeys();
  if (info && info->Has(vtkProp::GeneralTextureUnit()))
  {
    haveTextures = true;
  }

  // Transform the points, if necessary
  vtkPoints* p = poly->GetPoints();
  if (this->TransformCoordinate)
  {
    vtkIdType numPts = p->GetNumberOfPoints();
    if (!this->TransformedPoints)
    {
      this->TransformedPoints = vtkPoints::New();
    }
    this->TransformedPoints->SetNumberOfPoints(numPts);
    for (vtkIdType j = 0; j < numPts; j++)
    {
      this->TransformCoordinate->SetValue(p->GetPoint(j));
      if (this->TransformCoordinateUseDouble)
      {
        double* dtmp = this->TransformCoordinate->GetComputedDoubleViewportValue(viewport);
        this->TransformedPoints->SetPoint(j, dtmp[0], dtmp[1], 0.0);
      }
      else
      {
        int* itmp = this->TransformCoordinate->GetComputedViewportValue(viewport);
        this->TransformedPoints->SetPoint(j, itmp[0], itmp[1], 0.0);
      }
    }
    p = this->TransformedPoints;
  }

  // clear index arrays
  for (int primType = 0; primType < PrimitiveEnd; ++primType)
  {
    this->PrimitiveIndexArrays[primType].clear();
  }
  // populate index arrays
  typedef vtkOpenGLIndexBufferObject oglIdxUtils; // shorter
  oglIdxUtils::AppendPointIndexBuffer(this->PrimitiveIndexArrays[PrimitivePoints], prims[0], 0);
  oglIdxUtils::AppendLineIndexBuffer(this->PrimitiveIndexArrays[PrimitiveLines], prims[1], 0);
  oglIdxUtils::AppendTriangleIndexBuffer(
    this->PrimitiveIndexArrays[PrimitiveTris], prims[2], p, 0, nullptr, nullptr);
  oglIdxUtils::AppendStripIndexBuffer(
    this->PrimitiveIndexArrays[PrimitiveTriStrips], prims[3], 0, false);

  // populate vertex attributes
  auto expand = [](vtkSmartPointer<vtkDataArray> src, vtkSmartPointer<vtkDataArray> dst,
                  const unsigned int* indices, const std::size_t numIndices) {
    if (src == nullptr || dst == nullptr)
    {
      return;
    }
    vtkExpandVertexAttributes worker;
    using DispatchT = vtkArrayDispatch::Dispatch2BySameValueType<vtkArrayDispatch::AllTypes>;
    if (!DispatchT::Execute(src.Get(), dst.Get(), worker, indices, numIndices))
    {
      worker(src.Get(), dst.Get(), indices, numIndices);
    }
  };
  // 2D actors do not use normal/tangent based lighting effects.
  VertexAttributeArrays originalVAttribs;
  originalVAttribs.colors = c;
  originalVAttribs.points = p->GetData();
  originalVAttribs.tcoords = haveTextures ? poly->GetPointData()->GetTCoords() : nullptr;

  VertexAttributeArrays newVertexAttrs;
  newVertexAttrs = originalVAttribs;

  // unlike 3D actors, 2D actors do not have different kinds of representations,
  const std::size_t PrimitiveSizes[PrimitiveEnd] = {
    1, // points
    2, // lines
    3, // tris
    3, // tristrips
  };
  std::size_t primitiveStart = 0;
  for (int primType = 0; primType < PrimitiveEnd; ++primType)
  {
    auto& vbos = this->PrimitiveVBOGroup[primType];
    const auto& indexArray = this->PrimitiveIndexArrays[primType];
    const std::size_t numIndices = this->PrimitiveIndexArrays[primType].size();
    if (!numIndices)
    {
      continue;
    }
    const auto numPrimitives = numIndices / PrimitiveSizes[primType];
    newVertexAttrs.Resize(numIndices);
    const auto start = indexArray.data() + 0;
    expand(originalVAttribs.colors, newVertexAttrs.colors, start, numIndices);
    expand(originalVAttribs.points, newVertexAttrs.points, start, numIndices);
    expand(originalVAttribs.tcoords, newVertexAttrs.tcoords, start, numIndices);
    if (newVertexAttrs.points != nullptr)
    {
      vbos->CacheDataArray("vertexWC", newVertexAttrs.points, viewport, VTK_FLOAT);
    }
    if (newVertexAttrs.colors != nullptr)
    {
      vbos->CacheDataArray("diffuseColor", newVertexAttrs.colors, viewport, VTK_UNSIGNED_CHAR);
    }
    else if (this->HaveCellScalars)
    {
      const int numComp = this->Colors->GetNumberOfComponents();
      assert(numComp == 4);
      vtkNew<vtkUnsignedCharArray> cellColors;
      cellColors->SetNumberOfComponents(4);
      for (std::size_t i = 0; i < numPrimitives; ++i)
      {
        // repeat for every corner of the primitive.
        const vtkIdType destID = this->CellCellMap->GetValue(i + primitiveStart) * numComp;
        for (std::size_t j = 0; j < PrimitiveSizes[primType]; ++j)
        {
          cellColors->InsertNextTypedTuple(this->Colors->GetPointer(destID));
        }
      }
      vbos->CacheDataArray("diffuseColor", cellColors, viewport, VTK_UNSIGNED_CHAR);
    }
    if (newVertexAttrs.tcoords != nullptr)
    {
      vbos->CacheDataArray("tcoordMC", newVertexAttrs.tcoords, viewport, VTK_FLOAT);
    }
    vbos->BuildAllVBOs(viewport);
    primitiveStart += numPrimitives;
  }
  this->VBOUpdateTime.Modified();
}
