#include "AgentModel2D.h"

#include "FrameElement.h"
#include "TrajectoryData.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <vtkFloatArray.h>
#include <vtkIntArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>

AgentModel2D::AgentModel2D(TrajectoryData * trajectoryData) :
    _polyData(vtkPolyData::New()), _trajectoryData(trajectoryData)
{
}

void AgentModel2D::updatePolyData()
{
    vtkNew<vtkPoints> points;

    vtkNew<vtkFloatArray> colors;
    colors->SetName("color");
    colors->SetNumberOfComponents(1);

    vtkNew<vtkFloatArray> tensors;
    tensors->SetName("tensors");
    tensors->SetNumberOfComponents(9);

    vtkNew<vtkIntArray> labels;
    labels->SetName("labels");
    labels->SetNumberOfComponents(1);

    auto & elements = _trajectoryData->currentFrame()->GetFrameElements();

    for(const auto & element : elements) {
        const glm::dvec3 pos = element->pos;
        points->InsertNextPoint(pos.x, pos.y, pos.z);
        labels->InsertNextValue(element->id);

        const double color = element->color;
        if(color == -1) {
            colors->InsertNextValue(NAN);
        } else {
            colors->InsertNextValue(color / 255.0);
        }

        glm::dvec3 rad = element->radius;
        rad /= glm::dvec3{30, 30, 120};

        const auto scale = glm::scale(glm::dmat4x4{1.0}, rad);

        glm::dvec3 rot = element->orientation;
        rot[2]         = vtkMath::RadiansFromDegrees(rot[2]);

        glm::dmat4x4 transform = glm::rotate(glm::dmat4x4{1.0}, rot.x, {1.0, 0.0, 0.0});
        transform              = glm::rotate(transform, rot.y, {0.0, 1.0, 0.0});
        transform              = glm::rotate(transform, rot.z, {0.0, 0.0, 1.0});
        transform              = scale * transform;
        tensors->InsertNextTuple9(
            transform[0][0],
            transform[0][1],
            transform[0][2],
            transform[1][0],
            transform[1][1],
            transform[1][2],
            transform[2][0],
            transform[2][1],
            transform[2][2]);
    }

    _polyData->SetPoints(points);
    _polyData->GetPointData()->AddArray(colors);
    _polyData->GetPointData()->SetActiveScalars("color");

    _polyData->GetPointData()->SetTensors(tensors);
    _polyData->GetPointData()->SetActiveTensors("tensors");

    _polyData->GetPointData()->AddArray(labels);
}
