#
# Demonstrate the use of clipping and cutting
#
source vtkInt.tcl
source "colors.tcl"
source vtkInclude.tcl

# create test data by hand
#
vtkFloatPoints pts;
    pts InsertPoint 0 0 0 0;
    pts InsertPoint 1 1 0 0;
    pts InsertPoint 2 2 0 0;
    pts InsertPoint 3 3 0 0;
    pts InsertPoint 4 0 1 0;
    pts InsertPoint 5 1 1 0;
    pts InsertPoint 6 2 1 0;
    pts InsertPoint 7 3 1 0;
    pts InsertPoint 8 0 2 0;
    pts InsertPoint 9 1 2 0;
    pts InsertPoint 10 2 2 0;
    pts InsertPoint 11 3 2 0;
    pts InsertPoint 12 1 3 0;
    pts InsertPoint 13 2 3 0;
    pts InsertPoint 14 0 4 0;
    pts InsertPoint 15 1 4 0;
    pts InsertPoint 16 2 4 0;
    pts InsertPoint 17 3 4 0;
    pts InsertPoint 18 0 4 1;
    pts InsertPoint 19 1 4 1;
    pts InsertPoint 20 2 4 1;
    pts InsertPoint 21 3 4 1;
    pts InsertPoint 22 0 5 0;
    pts InsertPoint 23 1 5 0;
    pts InsertPoint 24 2 5 0;
    pts InsertPoint 25 3 5 0;
    pts InsertPoint 26 0 5 1;
    pts InsertPoint 27 1 5 1;
    pts InsertPoint 28 2 5 1;
    pts InsertPoint 29 3 5 1;

vtkIdList ids;
vtkUnstructuredGrid ug;
    ug Allocate 100 100;#initial amount and extend size
    ug SetPoints pts;

# create polygons
ids Reset; ids InsertNextId 0; ids InsertNextId 1; ids InsertNextId 5; ids InsertNextId 4;
ug InsertNextCell $VTK_QUAD ids;

ids Reset; ids InsertNextId 1; ids InsertNextId 2; ids InsertNextId 6; ids InsertNextId 5;
ug InsertNextCell $VTK_QUAD ids;

ids Reset; ids InsertNextId 2; ids InsertNextId 3; ids InsertNextId 7; ids InsertNextId 6;
ug InsertNextCell $VTK_QUAD ids;


# create a line and poly-line
ids Reset; ids InsertNextId 8; ids InsertNextId 9;
ug InsertNextCell $VTK_LINE ids;

ids Reset; ids InsertNextId 9; ids InsertNextId 10; ids InsertNextId 11;
ug InsertNextCell $VTK_POLY_LINE ids;


# create some points
ids Reset; ids InsertNextId 12; ids InsertNextId 13;
ug InsertNextCell $VTK_POLY_VERTEX ids;


# create some hexahedron
ids Reset; 
ids InsertNextId 14; ids InsertNextId 15; ids InsertNextId 23; ids InsertNextId 22;
ids InsertNextId 18; ids InsertNextId 19; ids InsertNextId 27; ids InsertNextId 26;
ug InsertNextCell $VTK_HEXAHEDRON ids;

ids Reset; 
ids InsertNextId 15; ids InsertNextId 16; ids InsertNextId 24; ids InsertNextId 23;
ids InsertNextId 19; ids InsertNextId 20; ids InsertNextId 28; ids InsertNextId 27;
ug InsertNextCell $VTK_HEXAHEDRON ids;

ids Reset; 
ids InsertNextId 16; ids InsertNextId 17; ids InsertNextId 25; ids InsertNextId 24;
ids InsertNextId 20; ids InsertNextId 21; ids InsertNextId 29; ids InsertNextId 28;
ug InsertNextCell $VTK_HEXAHEDRON ids;


# Create pipeline
#
vtkDataSetMapper meshMapper;
    meshMapper SetInput ug;
vtkActor meshActor;
    meshActor SetMapper meshMapper;
    eval [meshActor GetProperty] SetColor $green;
    [meshActor GetProperty] SetOpacity 0.2;

vtkPlane plane;
    plane SetOrigin 1.75 0 0;
    plane SetNormal 1 0 0;
## comment out either cutter or clipper to see the different effects
#vtkCutter clipper;
#    clipper SetInput ug;
#    clipper SetCutFunction plane;
#    clipper SetValue 0 0.0;
#    clipper SetValue 1 1.2;
#    clipper DebugOn;
vtkClipper clipper;
    clipper SetInput ug;
    clipper SetClipFunction plane;
    clipper SetValue 0.0;
    clipper DebugOn;
vtkDataSetMapper clippedMapper;
    clippedMapper SetInput [clipper GetOutput];
vtkActor clippedActor
    clippedActor SetMapper clippedMapper;
    eval [clippedActor GetProperty] SetColor $red;

vtkExtractEdges extract;
    extract SetInput [clipper GetOutput];
vtkTubeFilter tubes;
    tubes SetInput [extract GetOutput];
    tubes SetRadius 0.02;
    tubes SetNumberOfSides 6;
vtkPolyMapper mapEdges;
    mapEdges SetInput [tubes GetOutput];
vtkActor edgeActor
    edgeActor SetMapper mapEdges;
eval [edgeActor GetProperty] SetColor $peacock;
    [edgeActor GetProperty] SetSpecularColor 1 1 1;
    [edgeActor GetProperty] SetSpecular 0.3;
    [edgeActor GetProperty] SetSpecularPower 20;
    [edgeActor GetProperty] SetAmbient 0.2;
    [edgeActor GetProperty] SetDiffuse 0.8;

vtkSphereSource ball;
    ball SetRadius 0.05;
    ball SetThetaResolution 12;
    ball SetPhiResolution 12;
vtkGlyph3D balls;
    balls SetInput [clipper GetOutput];
    balls SetSource [ball GetOutput];
vtkPolyMapper mapBalls;
    mapBalls SetInput [balls GetOutput];
vtkActor ballActor
    ballActor SetMapper mapBalls;
    eval [ballActor GetProperty] SetColor $hot_pink;
    [ballActor GetProperty] SetSpecularColor 1 1 1;
    [ballActor GetProperty] SetSpecular 0.3;
    [ballActor GetProperty] SetSpecularPower 20;
    [ballActor GetProperty] SetAmbient 0.2;
    [ballActor GetProperty] SetDiffuse 0.8;


# Create graphics stuff
#
vtkRenderMaster rm;
set renWin [rm MakeRenderWindow];
set ren1   [$renWin MakeRenderer];
set iren [$renWin MakeRenderWindowInteractor];

# Add the actors to the renderer, set the background and size
#
$ren1 AddActors ballActor;
$ren1 AddActors edgeActor;
$ren1 AddActors meshActor;
$ren1 AddActors clippedActor;
$ren1 SetBackground 1 1 1;
$renWin SetSize 500 500;
$iren Initialize;

# render the image
#
$iren SetUserMethod {wm deiconify .vtkInteract};

# prevent the tk window from showing up then start the event loop
wm withdraw .


