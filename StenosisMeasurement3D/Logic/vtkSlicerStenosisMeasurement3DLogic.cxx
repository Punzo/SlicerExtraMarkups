/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// StenosisMeasurement3D Logic includes
#include "vtkSlicerStenosisMeasurement3DLogic.h"

// MRML includes
#include <vtkMRMLScene.h>

// VTK includes
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPlane.h>
#include <vtkClipPolyData.h>
#include <vtkClipClosedSurface.h>
#include <vtkPlaneCollection.h>
#include <vtkTriangleFilter.h>

// STD includes
#include <cassert>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerStenosisMeasurement3DLogic);

//----------------------------------------------------------------------------
vtkSlicerStenosisMeasurement3DLogic::vtkSlicerStenosisMeasurement3DLogic()
{
}

//----------------------------------------------------------------------------
vtkSlicerStenosisMeasurement3DLogic::~vtkSlicerStenosisMeasurement3DLogic()
{
}

//----------------------------------------------------------------------------
void vtkSlicerStenosisMeasurement3DLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkSlicerStenosisMeasurement3DLogic::SetMRMLSceneInternal(vtkMRMLScene * newScene)
{
  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  events->InsertNextValue(vtkMRMLScene::NodeRemovedEvent);
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEventsInternal(newScene, events.GetPointer());
}

//-----------------------------------------------------------------------------
void vtkSlicerStenosisMeasurement3DLogic::RegisterNodes()
{
  assert(this->GetMRMLScene() != 0);
}

//---------------------------------------------------------------------------
void vtkSlicerStenosisMeasurement3DLogic::UpdateFromMRMLScene()
{
  assert(this->GetMRMLScene() != 0);
}

//---------------------------------------------------------------------------
void vtkSlicerStenosisMeasurement3DLogic
::OnMRMLSceneNodeAdded(vtkMRMLNode* vtkNotUsed(node))
{
}

//---------------------------------------------------------------------------
void vtkSlicerStenosisMeasurement3DLogic
::OnMRMLSceneNodeRemoved(vtkMRMLNode* vtkNotUsed(node))
{
}

//---------------------------------------------------------------------------
bool vtkSlicerStenosisMeasurement3DLogic::Process(vtkMRMLMarkupsShapeNode * wall,
                                            vtkMRMLSegmentationNode * lumen, std::string segmentID,
                                            vtkMRMLMarkupsFiducialNode * boundary,
                                            vtkPolyData * wallOpenOut, vtkPolyData * lumenOpenOut,
                                            vtkPolyData * wallClosedOut, vtkPolyData * lumenClosedOut)
{
  // N.B. : we don't call ::UpdateBoundaryControlPointPosition here.
  if (wall == nullptr || lumen == nullptr || segmentID.empty() || boundary == nullptr
    || wall->GetNumberOfControlPoints() < 4 || boundary->GetNumberOfControlPoints() < 2
    || wall->GetShapeName() != vtkMRMLMarkupsShapeNode::Tube
  )
  {
    vtkErrorMacro("Invalid input.");
    return false;
  }
  
  // Get wall polydata from shape markups node.
  vtkPolyData * wallOpenSurface = wall->GetShapeWorld();
  vtkPolyData * wallClosedSurface = wall->GetCappedTubeWorld();
  // Generate lumen polydata from lumen segment.
  vtkNew<vtkPolyData> lumenSurface;
  if (!lumen->GetClosedSurfaceRepresentation(segmentID, lumenSurface))
  {
    if (!lumen->CreateClosedSurfaceRepresentation())
    {
      vtkErrorMacro("Cannot create closed surface from segmentation.");
      return false;
    }
    lumen->GetClosedSurfaceRepresentation(segmentID, lumenSurface);
  }
  
  // The first 2 fiducial points are used to cut through the lumen and wall polydata at arbitrary positions.
  double p1[3] = { 0.0 };
  double p2[3] = { 0.0 };
  boundary->GetNthControlPointPositionWorld(0, p1);
  boundary->GetNthControlPointPositionWorld(1, p2);
  
  // Get spline polydata from shape markups node.
  vtkPolyData * spline = wall->GetSplineWorld();
  vtkPoints * splinePoints = spline->GetPoints();
  // Get boundaries where polydatas will be cut.
  const vtkIdType p1IdType = spline->FindPoint(p1);
  const vtkIdType p2IdType = spline->FindPoint(p2);
  
  // Get adjacent points to boundaries to calculate normals.
  /*
   * N.B. : GetPoint() has a nasty documented version,
   * when result is assigned to a pointer.
   * A first result takes the value of next ones !
   */
  double p1Neighbour[3] = { 0.0 };
  splinePoints->GetPoint(p1IdType + 1, p1Neighbour);
  double p2Neighbour[3] = { 0.0 };
  splinePoints->GetPoint(p2IdType - 1, p2Neighbour);
  // If p1 is nearer to the end of the spline than p2.
  if (p1IdType > p2IdType)
  {
    splinePoints->GetPoint(p1IdType - 1, p1Neighbour);
    splinePoints->GetPoint(p2IdType + 1, p2Neighbour);
  }
  // Use as normals.
  double startDirection[3] = { 0.0 };
  double endDirection[3] = { 0.0 };
  // The normal 'looks' at the first parameter.
  vtkMath::Subtract(p1Neighbour, p1, startDirection);
  vtkMath::Subtract(p2Neighbour, p2, endDirection);
  
  // Open surface : Clip wall and lumen at p1. Clip the result at p2.
  vtkNew<vtkPolyData> wallIntermediate;
  this->Clip(wallOpenSurface, wallIntermediate, p1, startDirection, false);
  this->Clip(wallIntermediate, wallOpenOut, p2, endDirection, false);
  
  vtkNew<vtkPolyData> lumenIntermediate;
  this->Clip(lumenSurface, lumenIntermediate, p1, startDirection, false);
  this->Clip(lumenIntermediate, lumenOpenOut, p2, endDirection, false);
  
  // Closed surface
  this->ClipClosed(wallClosedSurface, wallClosedOut, p1, startDirection, p2, endDirection);
  this->ClipClosed(lumenSurface, lumenClosedOut, p1, startDirection, p2, endDirection);
  
  return true;
}

//---------------------------------------------------------------------------
bool vtkSlicerStenosisMeasurement3DLogic::Clip(vtkPolyData * input, vtkPolyData * output,
                                               double * origin, double * normal, bool clipped)
{
  if (input == nullptr || origin == NULL || normal == NULL)
  {
    vtkErrorMacro("Can't clip, invalid parameters.");
    return false;
  }
  vtkNew<vtkPlane> plane;
  plane->SetOrigin(origin);
  plane->SetNormal(normal);
  
  vtkNew<vtkClipPolyData> clipper;
  clipper->SetClipFunction(plane);
  clipper->SetInputData(input);
  clipper->GenerateClippedOutputOn();
  clipper->Update();
  
  if (clipped)
  {
    output->DeepCopy(clipper->GetClippedOutput());
  }
  else
  {
    output->DeepCopy(clipper->GetOutput());
  }
  return true;
}

//-----------------------------------------------------------------------------
bool vtkSlicerStenosisMeasurement3DLogic::UpdateBoundaryControlPointPosition
  (int pointIndex, vtkMRMLMarkupsFiducialNode * fiducialNode, vtkMRMLMarkupsShapeNode * shapeNode)
{
  // Put a ficucial point on the nearest point of the wall spline.
  if (pointIndex < 0 || fiducialNode == nullptr || shapeNode == nullptr
    || pointIndex > (fiducialNode->GetNumberOfControlPoints() - 1) )
  {
    vtkErrorMacro("Can't update control point position, invalid parameters.");
    return false;
  }
  vtkPolyData * spline = shapeNode->GetSplineWorld();
  double controlPointCoordinate[3] = { 0.0 };
  fiducialNode->GetNthControlPointPositionWorld(pointIndex, controlPointCoordinate);
  vtkIdType targetPointId = spline->FindPoint(controlPointCoordinate);
  double * targetPointCoordinate = spline->GetPoint(targetPointId);
  if (controlPointCoordinate[0] != targetPointCoordinate[0]
    || controlPointCoordinate[1] != targetPointCoordinate[1]
    || controlPointCoordinate[2] != targetPointCoordinate[2])
  {
    fiducialNode->SetNthControlPointPositionWorld(pointIndex, targetPointCoordinate);
  }
  return true;
}

//-----------------------------------------------------------------------------
bool vtkSlicerStenosisMeasurement3DLogic::ClipClosed(vtkPolyData * input, vtkPolyData * output,
            double * startOrigin, double * startNormal, double * endOrigin, double * endNormal)
{
  if (input == nullptr || startOrigin == NULL || startNormal == NULL
    || endOrigin == NULL || endNormal == NULL
  )
  {
    vtkErrorMacro("Can't clip, invalid parameters.");
    return false;
  }
  vtkNew<vtkPlane> startPlane;
  startPlane->SetOrigin(startOrigin);
  startPlane->SetNormal(startNormal);
  vtkNew<vtkPlane> endPlane;
  endPlane->SetOrigin(endOrigin);
  endPlane->SetNormal(endNormal);
  vtkNew<vtkPlaneCollection> planes;
  planes->AddItem(startPlane);
  planes->AddItem(endPlane);
  planes->Modified();
  
  vtkNew<vtkClipClosedSurface> clipper;
  clipper->SetClippingPlanes(planes);
  clipper->SetInputData(input);
  clipper->Update();
  
  vtkNew<vtkTriangleFilter> triangleFilter;
  triangleFilter->SetInputData(clipper->GetOutput());
  triangleFilter->Update();
  output->DeepCopy(triangleFilter->GetOutput());
  
  return true;
}
