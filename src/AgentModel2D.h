#pragma once

#include "TrajectoryData.h"

#include <vtkPolyData.h>

class AgentModel2D
{
    vtkSmartPointer<vtkPolyData> _polyData;
    TrajectoryData * _trajectoryData;

public:
    AgentModel2D(TrajectoryData * trajectoryData);
    ~AgentModel2D() = default;

    void updatePolyData();
    vtkSmartPointer<vtkPolyData> polyData() const { return _polyData; };
};
