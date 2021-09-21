/**
 * @file ThreadDataTransfert.cpp
 * @author   Ulrich Kemloh <kemlohulrich@gmail.com>
 * @version 0.1
 * Copyright (C) <2009-2010>
 *
 * @section LICENSE
 * This file is part of OpenPedSim.
 *
 * OpenPedSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenPedSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenPedSim. If not, see <http://www.gnu.org/licenses/>.
 *
 * @section DESCRIPTION
 * This class implements the thread that manages the main visulisation
 *
 * \brief visualise the data
 *
 *
 *
 *  Created on: 22.05.2009
 *
 */


#include <QThread>
#include <QString>
#include <QObject>
#include <QMessageBox>

#include <vtkRenderer.h>
#include <vtkCamera.h>
#include <vtkAssembly.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkAxisActor.h>
#include <vtkAxesActor.h>
#include <vtkTextActor.h>
#include <vtkLight.h>
#include <vtkScalarBarActor.h>
#include <vtkFloatArray.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkLookupTable.h>
#include <vtkTextProperty.h>
#include <vtkProperty.h>
#include <vtkOutputWindow.h>
#include <vtkFileOutputWindow.h>
#include <vtkCallbackCommand.h>
#include <vtkSmartPointer.h>
#include <vtkActor2DCollection.h>
#include <vtkRegularPolygonSource.h>
#include <vtkLabeledDataMapper.h>
#include <vtkDiskSource.h>
#include <vtkConeSource.h>
#include <vtkTriangleFilter.h>
#include <vtkStripper.h>
#include <vtkSphereSource.h>
#include <vtkCylinderSource.h>
#include <vtkWindowToImageFilter.h>
#include <vtkActor.h>
#include <vtkLightKit.h>
#include <vtkPolyLine.h>

#include "geometry/FacilityGeometry.h"
#include "geometry/GeometryFactory.h"
#include "geometry/LinePlotter2D.h"

#include "ThreadVisualisation.h"
#include "Pedestrian.h"
#include "TimerCallback.h"
#include "Frame.h"
#include "TrajectoryPoint.h"
#include "SyncData.h"
#include "InteractorStyle.h"
#include "SystemSettings.h"
#include "geometry/PointPlotter.h"
#include "Log.h"


#define VTK_CREATE(type, name) \
    vtkSmartPointer<type> name = vtkSmartPointer<type>::New()


ThreadVisualisation::ThreadVisualisation(QObject* parent, vtkRenderWindow* renderWindow):
    QObject(parent)
{
    _renderWindow = renderWindow;
    _renderer=NULL;
    _runningTime=vtkTextActor::New();;
    _framePerSecond=25;
    _axis=NULL;
    _winTitle="header without room caption";
}

ThreadVisualisation::~ThreadVisualisation()
{

    if(_axis)
        _axis->Delete();

    if(extern_glyphs_pedestrians_3D) extern_glyphs_pedestrians_3D->Delete();
    if(extern_glyphs_pedestrians_actor_3D) extern_glyphs_pedestrians_actor_3D->Delete();
    if(extern_glyphs_pedestrians) extern_glyphs_pedestrians->Delete();
    if(extern_glyphs_pedestrians_actor_2D) extern_glyphs_pedestrians_actor_2D->Delete();
    if(extern_pedestrians_labels) extern_pedestrians_labels->Delete();
    //show directions of movement
    if(extern_glyphs_directions) extern_glyphs_directions->Delete();
    if(extern_glyphs_directions_actor) extern_glyphs_directions_actor->Delete();

    _runningTime->Delete();

}

void ThreadVisualisation::setFullsreen(bool status)
{
}

void ThreadVisualisation::slotSetFrameRate(float fps)
{
    _framePerSecond=fps;
}


void ThreadVisualisation::run()
{
    //deactivate the output windows
    vtkObject::GlobalWarningDisplayOff();

    // Create the renderer
    _renderer = vtkRenderer::New();
    // set the background
    _renderer->SetBackground(1.0,1.0,1.0);
    //add the geometry
    _geometry.Init(_renderer);

    initGlyphs2D();
    initGlyphs3D();

    //create the trails
    extern_trail_plotter = new PointPlotter();
    _renderer->AddActor(extern_trail_plotter->getActor());

    // Create the render window
    _renderWindow->AddRenderer( _renderer );

    //add the running time frame
    _runningTime->SetTextScaleModeToViewport();
    _runningTime->SetVisibility(SystemSettings::getOnScreenInfos());

    // set the properties of the caption
    vtkTextProperty* tprop = _runningTime->GetTextProperty();
    tprop->SetFontSize(10);
    tprop->SetColor(1.0,0.0,0.0);

    _renderer->AddActor2D(_runningTime);

    // Create an interactor
    auto* interactor = _renderWindow->GetInteractor();
    interactor->SetInteractorStyle(&_interactorStyle);

    if(SystemSettings::get2D()) {
        _renderer->GetActiveCamera()->OrthogonalizeViewUp();
        _renderer->GetActiveCamera()->ParallelProjectionOn();
        _renderer->ResetCamera();
    }

    //create a timer for rendering the window
    _timer_cb = std::make_unique<TimerCallback>();
    _timer_id = interactor->CreateRepeatingTimer(1000.0/_framePerSecond);
    _timer_cb->SetRenderTimerId(_timer_id);
    _timer_cb->setTextActor(_runningTime);
    interactor->AddObserver(vtkCommand::TimerEvent,_timer_cb.get());





    //create the necessary connections
    QObject::connect(_timer_cb.get(), SIGNAL(signalRunningTime(unsigned long )),
                     this->parent(), SLOT(slotRunningTime(unsigned long )));

    QObject::connect(_timer_cb.get(), SIGNAL(signalFrameNumber(unsigned long, unsigned long )),
                     this->parent(), SLOT(slotFrameNumber(unsigned long, unsigned long )));

    QObject::connect(_timer_cb.get(), SIGNAL(signalRenderingTime(int)),
                     this->parent(), SLOT(slotRenderingTime(int)));

    //save the top view  camera
    _topViewCamera=vtkCamera::New();
    _topViewCamera->DeepCopy(_renderer->GetActiveCamera());

    //update all (restored) system settings
    setGeometryVisibility2D(SystemSettings::get2D());
    setGeometryVisibility3D(!SystemSettings::get2D());
    setGeometryVisibility(SystemSettings::getShowGeometry());
    setOnscreenInformationVisibility(SystemSettings::getOnScreenInfos());
    showFloor(SystemSettings::getShowFloor());
    showWalls(SystemSettings::getShowWalls());
    showObstacle(SystemSettings::getShowObstacles());
    showDoors(SystemSettings::getShowExits());
    showNavLines(SystemSettings::getShowNavLines());
    setGeometryLabelsVisibility(SystemSettings::getShowGeometryCaptions());
    setBackgroundColor(SystemSettings::getBackgroundColor());
    setWallsColor(SystemSettings::getWallsColor());
    setObstacleColor(SystemSettings::getObstacleColor());
    setFloorColor(SystemSettings::getFloorColor());
    setExitsColor(SystemSettings::getExitsColor());
    setNavLinesColor(SystemSettings::getNavLinesColor());
    //FIXME:
    showGradientField(SystemSettings::getShowGradientField());
    interactor->Start();

    emit signal_controlSequences("CONTROL_RESET");
}

void ThreadVisualisation::stop() {
    QObject::disconnect(_timer_cb.get(), SIGNAL(signalRunningTime(unsigned long )),
                     this->parent(), SLOT(slotRunningTime(unsigned long )));

    QObject::disconnect(_timer_cb.get(), SIGNAL(signalFrameNumber(unsigned long, unsigned long )),
                     this->parent(), SLOT(slotFrameNumber(unsigned long, unsigned long )));

    QObject::disconnect(_timer_cb.get(), SIGNAL(signalRenderingTime(int)),
                     this->parent(), SLOT(slotRenderingTime(int)));
    _renderWindow->GetInteractor()->DestroyTimer(_timer_id);
    _renderWindow->GetInteractor()->RemoveAllObservers();
}

void ThreadVisualisation::setGeometryVisibility( bool status)
{

    if(SystemSettings::get2D())
    {
        _geometry.Set2D(status);
    } else
    {
       _geometry.Set3D(status);
    }
}

/// show / hide the walls
void ThreadVisualisation::showWalls(bool status)
{
    _geometry.ShowWalls(status);
}

/// show/ hide the exits
void ThreadVisualisation::showDoors(bool status)
{
    _geometry.ShowDoors(status);
}

void ThreadVisualisation::showNavLines(bool status)
{
    _geometry.ShowNavLines(status);
}

void ThreadVisualisation::showFloor(bool status)
{
    _geometry.ShowFloor(status);
}

void ThreadVisualisation::showObstacle(bool status)
{
    _geometry.ShowObstacles(status);
}
void ThreadVisualisation::showGradientField(bool status)
{
    _geometry.ShowGradientField(status);
}
void  ThreadVisualisation::initGlyphs2D()
{
    if(extern_glyphs_pedestrians) extern_glyphs_pedestrians->Delete();
    if(extern_glyphs_pedestrians_actor_2D) extern_glyphs_pedestrians_actor_2D->Delete();
    if(extern_pedestrians_labels) extern_pedestrians_labels->Delete();
    //show directions of movement
    if(extern_glyphs_directions) extern_glyphs_directions->Delete();
    if(extern_glyphs_directions_actor) extern_glyphs_directions_actor->Delete();

    extern_glyphs_pedestrians = vtkTensorGlyph::New();
    extern_glyphs_pedestrians_actor_2D = vtkActor::New();
    extern_pedestrians_labels =  vtkActor2D::New();
    extern_glyphs_directions=vtkTensorGlyph::New();
    extern_glyphs_directions_actor=vtkActor::New();

    //now create the glyphs with ellipses
    VTK_CREATE (vtkDiskSource, agentShape);
    agentShape->SetCircumferentialResolution(20);
    agentShape->SetInnerRadius(0);
    agentShape->SetOuterRadius(30);

    //speed the rendering using triangles stripers
    VTK_CREATE(vtkTriangleFilter, tris);
    tris->SetInputConnection(agentShape->GetOutputPort());

    VTK_CREATE(vtkStripper, strip);
    strip->SetInputConnection(tris->GetOutputPort());

    extern_glyphs_pedestrians->SetSourceConnection(strip->GetOutputPort());

    //first frame
    auto&& frames=extern_trajectories_firstSet.GetFrames();
    if(frames.empty()) return;

    Frame * frame = frames.begin()->second;
    vtkPolyData* pData=NULL;

    if(frame) pData=frame->GetPolyData2D();
    
    extern_glyphs_pedestrians->SetInputConnection(agentShape->GetOutputPort());
    if (frame) extern_glyphs_pedestrians->SetInputData(pData);
    extern_glyphs_pedestrians->ThreeGlyphsOff();
    extern_glyphs_pedestrians->ExtractEigenvaluesOff();

    VTK_CREATE(vtkPolyDataMapper, mapper);
    mapper->SetInputConnection(extern_glyphs_pedestrians->GetOutputPort());

    VTK_CREATE(vtkLookupTable, lut);
    lut->SetHueRange(0.0,0.470);
    lut->SetValueRange(1.0,1.0);
    lut->SetNanColor(0.2,0.2,0.2,0.5);
    lut->SetNumberOfTableValues(256);
    lut->Build();
    mapper->SetLookupTable(lut);

    extern_glyphs_pedestrians_actor_2D->SetMapper(mapper);

    _renderer->AddActor(extern_glyphs_pedestrians_actor_2D);

    //Show directions
    VTK_CREATE(vtkConeSource,agentDirection);
    agentDirection->SetHeight(40);
    agentDirection->SetRadius(15);

    VTK_CREATE(vtkTriangleFilter, tris2);
    tris2->SetInputConnection(agentDirection->GetOutputPort());
    VTK_CREATE(vtkStripper, strip2);
    strip2->SetInputConnection(tris2->GetOutputPort());

    extern_glyphs_directions->SetSourceConnection(strip2->GetOutputPort());
    extern_glyphs_directions->SetInputConnection(agentDirection->GetOutputPort());
    if (frame) extern_glyphs_directions->SetInputData(pData);
    extern_glyphs_directions->ThreeGlyphsOff();
    extern_glyphs_directions->ExtractEigenvaluesOff();

    VTK_CREATE(vtkPolyDataMapper, mapper2);
    mapper2->SetInputConnection(extern_glyphs_directions->GetOutputPort());
    mapper2->ScalarVisibilityOff(); //to set color
    mapper2->SetLookupTable(lut);

    extern_glyphs_directions_actor->SetMapper(mapper2);
    extern_glyphs_directions_actor->GetProperty()->SetColor(0,0,0); //black
    _renderer->AddActor2D(extern_glyphs_directions_actor);

    // structure for the labels
    VTK_CREATE(vtkLabeledDataMapper, labelMapper);
    extern_pedestrians_labels->SetMapper(labelMapper);
    labelMapper->SetFieldDataName("labels");
    labelMapper->SetLabelModeToLabelFieldData();
    _renderer->AddActor2D(extern_pedestrians_labels);
    extern_pedestrians_labels->SetVisibility(false);
}

void ThreadVisualisation::initGlyphs3D()
{

    if(extern_glyphs_pedestrians_3D) extern_glyphs_pedestrians_3D->Delete();
    if(extern_glyphs_pedestrians_actor_3D) extern_glyphs_pedestrians_actor_3D->Delete();

    extern_glyphs_pedestrians_3D = vtkTensorGlyph::New();
    extern_glyphs_pedestrians_actor_3D = vtkActor::New();

    //now create the glyphs with zylinders
    VTK_CREATE (vtkCylinderSource, agentShape);
    agentShape->SetHeight(160);
    agentShape->SetRadius(20);
    agentShape->SetResolution(20);

    //speed the rendering using triangles stripers
    VTK_CREATE(vtkTriangleFilter, tris);
    tris->SetInputConnection(agentShape->GetOutputPort());

    VTK_CREATE(vtkStripper, strip);
    strip->SetInputConnection(tris->GetOutputPort());

    extern_glyphs_pedestrians_3D->SetSourceConnection(strip->GetOutputPort());

    //first frame
    auto&& frames=extern_trajectories_firstSet.GetFrames();
    if(frames.empty()) return;

    Frame * frame = frames.begin()->second;

    vtkPolyData* pData=NULL;
    if(frame) pData=frame->GetPolyData2D();

    extern_glyphs_pedestrians_3D->SetInputConnection(strip->GetOutputPort());
    if (frame )extern_glyphs_pedestrians_3D->SetInputData(pData);

    extern_glyphs_pedestrians_3D->ThreeGlyphsOff();
    extern_glyphs_pedestrians_3D->ExtractEigenvaluesOff();

    VTK_CREATE(vtkPolyDataMapper, mapper);
    mapper->SetInputConnection(extern_glyphs_pedestrians_3D->GetOutputPort());

    VTK_CREATE(vtkLookupTable, lut);
    lut->SetHueRange(0.0,0.470);
    lut->SetValueRange(1.0,1.0);
    lut->SetNanColor(0.2,0.2,0.2,0.5);
    lut->SetNumberOfTableValues(256);
    lut->Build();
    mapper->SetLookupTable(lut);

    extern_glyphs_pedestrians_actor_3D->SetMapper(mapper);
    extern_glyphs_pedestrians_actor_3D->GetProperty()->BackfaceCullingOn();
    _renderer->AddActor(extern_glyphs_pedestrians_actor_3D);

    extern_glyphs_pedestrians_actor_3D->SetVisibility(false);
}

void  ThreadVisualisation::init()
{
}

void ThreadVisualisation::finalize()
{

}

void ThreadVisualisation::QcolorToDouble(const QColor &col, double *rgb)
{
    rgb[0]=(double)col.red()/255.0;
    rgb[1]=(double)col.green()/255.0;
    rgb[2]=(double)col.blue()/255.0;
}

void ThreadVisualisation::initLegend(/*std::vector scalars*/)
{
    //lookup table
    vtkLookupTable* lut =  vtkLookupTable::New();
    lut->SetHueRange(0.0,0.566);
    lut->SetTableRange(20.0, 50.0);
    lut->SetNumberOfTableValues(50);
    lut->Build();

    vtkTextProperty* titleProp = vtkTextProperty::New();
    titleProp->SetFontSize(14);

    vtkTextProperty* labelProp = vtkTextProperty::New();
    labelProp->SetFontSize(10);

    vtkScalarBarActor* scalarBar = vtkScalarBarActor::New();
    scalarBar->SetLookupTable(lut);
    scalarBar->SetTitle("Velocities ( cm/s )");
    scalarBar->SetTitleTextProperty(titleProp);
    scalarBar->SetLabelTextProperty (labelProp);
    scalarBar->GetPositionCoordinate()->SetCoordinateSystemToNormalizedViewport();
    scalarBar->GetPositionCoordinate()->SetValue(0.005, 0.90075);
    scalarBar->SetOrientationToVertical();
    scalarBar->SetNumberOfLabels(7);
    scalarBar->SetMaximumNumberOfColors(20);
    scalarBar->SetWidth(0.105);
    scalarBar->SetHeight(0.10);
    _renderer->AddActor2D(scalarBar);
    _renderer->Render();

}


void ThreadVisualisation::setAxisVisible(bool status)
{
    _axis->SetVisibility(status);
}

void ThreadVisualisation::setCameraPerspective(int mode,int degree)
{
    if(_renderer==NULL) return;

    switch (mode) {
    case 1: //TOP oder RESET
        _renderer->GetActiveCamera()->DeepCopy(_topViewCamera);
        break;
    case 2://TOP Rotate [range:-180-->180]
        _topViewCamera->Roll(degree);
        _renderer->GetActiveCamera()->DeepCopy(_topViewCamera);
        _topViewCamera->Roll(-degree);
        break;
    case 3://Side Rotate [range:-80-->80]
        _topViewCamera->Elevation(-degree);
        _renderer->GetActiveCamera()->DeepCopy(_topViewCamera);
        _topViewCamera->Elevation(degree);
        break;
    case 4: { // agent virtual reality
        //vtkCamera *camera = renderer->GetActiveCamera();
        //camera->SetRoll(-90);
        //std::cout <<"roll       [ "<< camera->GetRoll()<<" ]"<<std::endl;
        //double roll=camera->GetRoll();
        //camera->Roll(-90-camera->GetRoll());
        //camera->Modified();
    }

        break;
    }
    _renderer->ResetCamera();

}

void ThreadVisualisation::setBackgroundColor(const QColor& col)
{
    double  bgcolor[3];
    QcolorToDouble(col,bgcolor);
    if (_renderer!=NULL)
        _renderer->SetBackground(bgcolor);
}

void ThreadVisualisation::setWindowTitle(QString title)
{
    if(title.isEmpty())return;
    _winTitle=title;
}

/// @todo check this construct
void ThreadVisualisation::setGeometry(FacilityGeometry* geometry)
{
    cout<<"dont call me"<<endl;
    exit(0);
}

GeometryFactory &ThreadVisualisation::getGeometry()
{
    return _geometry;
}

void ThreadVisualisation::setWallsColor(const QColor &color)
{
    double  rbgColor[3];
    QcolorToDouble(color,rbgColor);
    _geometry.ChangeWallsColor(rbgColor);
}

void ThreadVisualisation::setFloorColor(const QColor &color)
{
    double  rbgColor[3];
    QcolorToDouble(color,rbgColor);
    _geometry.ChangeFloorColor(rbgColor);
}

void ThreadVisualisation::setObstacleColor(const QColor &color)
{
    double  rbgColor[3];
    QcolorToDouble(color,rbgColor);
    _geometry.ChangeObstaclesColor(rbgColor);
}

void ThreadVisualisation::setGeometryLabelsVisibility(int v)
{
    _geometry.ShowGeometryLabels(v);
}

void ThreadVisualisation::setExitsColor(const QColor &color)
{
    double  rbgColor[3];
    QcolorToDouble(color,rbgColor);
// HH
    _geometry.ChangeExitsColor(rbgColor);
}

void ThreadVisualisation::setNavLinesColor(const QColor &color)
{
    double  rbgColor[3];
    QcolorToDouble(color,rbgColor);
    _geometry.ChangeNavLinesColor(rbgColor);
}

/// enable/disable 2D
/// todo: consider disabling the 2d option in the 3d, and vice-versa
void ThreadVisualisation::setGeometryVisibility2D(bool status)
{
    _geometry.Set2D(status);
}

/// enable/disable 3D
/// todo: consider disabling the 2d option in the 3d, and vice-versa
void ThreadVisualisation::setGeometryVisibility3D(bool status)
{
    _geometry.Set3D(status);
}

void ThreadVisualisation::setOnscreenInformationVisibility(bool show)
{
    _runningTime->SetVisibility(show);
}

void ThreadVisualisation::Create2dAgent()
{

}
