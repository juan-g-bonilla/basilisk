/*
ISC License

Copyright (c) 2019, Autonomous Vehicle Systems Lab, University of Colorado at Boulder

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/


#ifndef RigidBodyContactEffector_h
#define RigidBodyContactEffector_h

#include <Eigen/Dense>
#include <vector>
#include <tuple>
#include <set>
#include <iostream>
#include <stack>
#include <stdio.h>
#include <cmath>
#include <algorithm>
#include <tiny_obj_loader.h>
#include "simulation/dynamics/_GeneralModuleFiles/dynamicEffector.h"
#include "simulation/dynamics/_GeneralModuleFiles/stateData.h"
#include "architecture/_GeneralModuleFiles/sys_model.h"
#include "architecture/msgPayloadDefC/SCStatesMsgPayload.h"
#include "architecture/msgPayloadDefC/SCMassPropsMsgPayload.h"
#include "architecture/msgPayloadDefC/SpicePlanetStateMsgPayload.h"
#include "architecture/messaging/messaging.h"
#include "architecture/utilities/bskLogging.h"
#include "architecture/utilities/macroDefinitions.h"
#include "architecture/utilities/avsEigenMRP.h"
#include "architecture/utilities/rigidBodyKinematics.h"


typedef struct{
    std::tuple<int, int> parentIndices;
    std::vector<std::tuple<int, int>> overlaps;
}boundingBoxDetail;

/*! Struct for holding the bounds of a vector over a time interval.*/
typedef struct{
    Eigen::Vector3d lower;                      //!< -- Lower time bound
    Eigen::Vector3d upper;                      //!< -- Upper time bound
}vectorInterval;

typedef struct{
    vectorInterval xAxisInterval;
    vectorInterval yAxisInterval;
    vectorInterval zAxisInterval;
    Eigen::Vector3d halfSize;
}indivBoundingBox;

/*! Struct for holding primitive information of a single polyhedron in a half edge format*/
typedef struct{
    std::vector<Eigen::Vector3d> faceNormals;   //!< -- Normal vectors of each face
    std::vector<std::vector<int>> faceTriangles;//!< -- Indices for vertices of each triangle
    std::vector<Eigen::Vector3d> faceCentroids;
    std::vector<Eigen::Vector3d> faceBoundingBoxes;
    std::vector<double> faceBoundingRadius;
    std::vector<std::vector<int>> edgeIndices;  //!< -- Indicies for the verticies of each edge
    std::vector<std::tuple<int, int, int>> faceIndices;               //!< -- Indicies for each face connecting to an edge
    Eigen::Vector3d centroid;                   //!< [m] Centroid of the polyhedron
    std::vector<int> uniqueVertIndices;
    Eigen::Vector3d boundingBox;
}halfEdge;

/*! Struct for holding dynamics data of each body*/
typedef struct{
    Eigen::Vector3d r_BN_N;                     //!< [m] Position of body wrt to base
    Eigen::Vector3d v_BN_N;                     //!< [m/s] Velocity of body wrt to base
    Eigen::Vector3d nonConservativeAccelpntB_B;
    double m_SC;                                //!< [kg] Mass of body
    Eigen::MatrixXd ISCPntB_B;                  //!<  [kg m^2] Inertia of body about point B in that body's frame
    Eigen::MatrixXd ISCPntB_B_inv;
    Eigen::Vector3d c_B;                        //!< [m] Vector from point B to CoM of body in body's frame
    Eigen::Vector3d omega_BN_B;                 //!< [r/s] Attitude rate of the body wrt base
    Eigen::Vector3d omegaDot_BN_B;
    Eigen::Matrix3d omegaTilde_BN_B;
    Eigen::MRPd sigma_BN;                       //!< -- Attitude of the body wrt base
    Eigen::MRPd sigma_BprimeB;                  //!< -- Linearly propegated attitude of the body wrt base
    Eigen::Matrix3d dcm_BprimeB;
    Eigen::Matrix3d dcm_BN;
    Eigen::Matrix3d dcm_NB;                        
}dynamicData;

/*! Struct for holding the linked states of each body*/
typedef struct{
    StateData *hubPosition;
    StateData *hubVelocity;
    StateData *hubSigma;
    StateData *hubOmega_BN_N;
    Eigen::MatrixXd *r_BN_N;
    Eigen::MatrixXd *v_BN_N;
    Eigen::MatrixXd *m_SC;
    Eigen::MatrixXd *ISCPntB_B;
    Eigen::MatrixXd *c_B;
}linkedStates;

/*! Struct for holding all required information of each body*/
typedef struct{
    double boundingRadius;                      //!< [m] Radius of body bounding sphere
    double coefRestitution;                     //!< -- Coefficient of Restitution between external body and main body
    double coefFriction;
    std::string objFile;                        //!< -- File name for the .obj file pertaining to body
    tinyobj::attrib_t attrib;                   //!< -- Attribute conversion from TinyOBJLoader
    std::vector<Eigen::Vector3d> vertices;      //!< -- All verticies in the body
    std::vector<tinyobj::shape_t> shapes;       //!< -- Polyhedra data from TinyOBJLoader
    std::vector<halfEdge> polyhedron;           //!< -- Half edge converted polyhedra data
    boundingBoxDetail coarseSearchList;
    dynamicData states;                         //!< -- Extracted states for the body
    dynamicData futureStates;
    std::string modelTag;                       //!< -- Name of body's model tag
    ReadFunctor<SpicePlanetStateMsgPayload> planetInMsg;
    SpicePlanetStateMsgPayload plMsg;
    bool isSpice;
    ReadFunctor<SCStatesMsgPayload> scStateInMsg;
    ReadFunctor<SCMassPropsMsgPayload> scMassStateInMsg;
    SCStatesMsgPayload stateInBuffer;           //!< -- Body state buffer
    SCMassPropsMsgPayload massStateInBuffer;    //!< -- Body mass state buffer
    std::vector<Eigen::Vector3d> forceExternal_N;
    std::vector<Eigen::Vector3d> torqueExternalPntB_B;
    std::vector<double> impactTimes;
    std::vector<double> impactTimeSteps;
}geometry;

/*! @brief Rigid Body Contact state effector class */
class RigidBodyContactEffector: public SysModel, public DynamicEffector
{
public:
    RigidBodyContactEffector();
    ~RigidBodyContactEffector();
    void Reset();
    void LoadSpacecraftBody(const char *objFile, std::string modelTag, Message<SCStatesMsgPayload> *scStateMsg, Message<SCMassPropsMsgPayload> *scMassStateMsg, double boundingRadius, double coefRestitution, double coefFriction);
    void AddSpiceBody(const char *objFile, Message<SpicePlanetStateMsgPayload> *planetSpiceMsg, double boundingRadius, double coefRestitution, double coefFriction);
    void linkInStates(DynParamManager& states);
    void computeForceTorque(double currentTime, double timeStep);
    void computeStateContribution(double integTime);
    void UpdateState(uint64_t CurrentSimNanos);
    void ReadInputs();
    void ExtractFromBuffer();
    void CheckBoundingSphere();
    void CheckBoundingBox();
    
private:
    double currentSimSeconds;
    std::vector<std::vector<int>> closeBodies;               //!< -- Indicies of all external bodies that the main body is within the bounding sphere of
    int currentBodyInCycle;
    double currentMinError;
    bool responseFound;
    bool lockedToRand;
    double timeFound;
    double integrateTimeStep;
    bool newMacroTimeStep;
    double topTime;
    double topTimeStep;
    bool secondInter;
    
public:
    std::vector<geometry> Bodies;
    int numBodies;
    int currentBody;
    double maxPosError;
    double slipTolerance;
    double simTimeStep;
    double collisionIntegrationStep;
    double maxBoundingBoxDim;
    double minBoundingBoxDim;
    double boundingBoxFF;
    double maxTimeStep;
    double timeSynchTol;
    
    
    
    
private:
    Eigen::VectorXd CollisionStateDerivative(Eigen::VectorXd X_c, std::vector<std::tuple<Eigen::Vector3d, Eigen::Vector3d, Eigen::Vector3d>> impacts, Eigen::MatrixXd M_tot, double coefRes, double coefFric);
    bool SeparatingPlane(vectorInterval displacementInterval, vectorInterval candidateInterval, indivBoundingBox box1, indivBoundingBox box2);
  
    std::vector<halfEdge> ComputeHalfEdge(std::vector<Eigen::Vector3d> vertices, std::vector<tinyobj::shape_t> shapes);

    std::vector<double> IntervalSine(double a, double b);
    std::vector<double> IntervalCosine(double a, double b);
    std::vector<double> IntervalDotProduct(vectorInterval vectorA, vectorInterval vectorB);
    vectorInterval IntervalCrossProduct(vectorInterval vectorA, vectorInterval vectorB);
    int LineLineDistance(Eigen::Vector3d vertex1, Eigen::Vector3d vertex2, Eigen::Vector3d vertex3, Eigen::Vector3d vertex4, Eigen::Vector3d *pointA, Eigen::Vector3d *pointB);
    int PointInTriangle(Eigen::Vector3d supportPoint, Eigen::Vector3d triVertex0, Eigen::Vector3d triVertex1, Eigen::Vector3d triVertex2, Eigen::Vector3d *contactPoint, double *distance);
    Eigen::Vector3d SecondTop(std::stack<Eigen::Vector3d> &stk);
    bool ComparePoints(const std::vector<Eigen::Vector3d> &point1, const std::vector<Eigen::Vector3d> &point2);
    std::vector<Eigen::Vector3d> findConvexHull(std::vector<Eigen::Vector3d> points);
};


#endif /* RigidBodyContactEffector_h */