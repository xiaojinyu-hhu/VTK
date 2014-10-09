vtk_module(vtkRenderingVolumeOpenGLNew
  DEPENDS
    vtkCommonCore
    vtkCommonDataModel
    vtkCommonExecutionModel
    vtkFiltersSources
    vtkglew
    vtkRenderingOpenGL
    vtkRenderingVolume
  IMPLEMENTS
    vtkRenderingVolume
  TEST_DEPENDS
    vtkCommonCore
    vtkFiltersModeling
    vtkglew
    vtkInteractionStyle
    vtkIOLegacy
    vtkIOXML
    vtkRenderingOpenGL
    vtkTestingCore
    vtkTestingRendering
  EXCLUDE_FROM_ALL
)
