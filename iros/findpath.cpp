/*
 */
#include <fstream>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <Model/HumanoidBodyUtil.h>
#include <hrpModel/JointPath.h>
#include <hrpModel/Link.h>
#include <hrpPlanner/RRT.h>
#include <hrpPlanner/Roadmap.h>
#include <hrpPlanner/RoadmapNode.h>
#include "problem.h"
#include "myCfgSetter.h"
#include <Math/Physics.h>

using namespace motion_generator;
using namespace hrp;


int main(int argc, char *argv[])
{
    srand((unsigned)time(NULL));
 
    const char *robotURL = NULL;
    std::vector<std::string> obstacleURL;
    std::vector<Vector3> obstacleP;
    std::vector<Vector3> obstacleRpy;
    for(int i = 1 ; i < argc; i++){
        if (strcmp(argv[i], "-robot") == 0){
            robotURL = argv[++i];
        }else if (strcmp(argv[i], "-obstacle") == 0){
            obstacleURL.push_back(argv[++i]);
            Vector3 p, rpy;
            for (int j=0; j<3; j++) p[j] = atof(argv[++i]);
            obstacleP.push_back(p);
            for (int j=0; j<3; j++) rpy[j] = atof(argv[++i]);
            obstacleRpy.push_back(rpy);
        }
    }
    if (robotURL == NULL){
        std::cerr << "please specify URL of VRML model by -robot option"
                  << std::endl;
        return 1;
    }


    HumanoidBodyPtr robot = new HumanoidBody();
    loadHumanoidBodyFromModelLoader(robot, robotURL, argc, argv, true);

    problem prob;
    prob.addRobot("robot", robotURL, robot);
    std::vector<BodyPtr> obstacles;
    for (unsigned int i=0; i<obstacleURL.size(); i++){
        char buf[20];
        sprintf(buf, "obstacle%02d", i);
        obstacles.push_back(prob.addObstacle(buf, obstacleURL[i]));
    }

    // This must be called after all bodies are added
    prob.initOLV(argc, argv);

    PathEngine::Configuration::size(4+6); 
    PathEngine::Configuration::bounds(0,  0.2, 0.8); // body z
    PathEngine::Configuration::bounds(1, -0.5, 0.5); // body roll
    PathEngine::Configuration::bounds(2, -0.0, 0.5); // body pitch
    PathEngine::Configuration::bounds(3, -0.5, 0.5); // body yaw

    int arm;
    PathEngine::Configuration goalCfg;
    std::ifstream ifs("goal.txt");
    ifs >> arm;
    for (unsigned int i=0; i<PathEngine::Configuration::size(); i++){
        ifs >> goalCfg[i];
    }
    
    PathEngine::Configuration::weight(0) = 10;

    PathEngine::Configuration::weight(1) = 1;
    PathEngine::Configuration::weight(2) = 1;
    PathEngine::Configuration::weight(3) = 1;

    PathEngine::Configuration::weight(4) = 0.8;
    PathEngine::Configuration::weight(5) = 0.6;
    PathEngine::Configuration::weight(6) = 0.4;
    PathEngine::Configuration::weight(7) = 0.3;
    PathEngine::Configuration::weight(8) = 0.2;
    PathEngine::Configuration::weight(9) = 0.1;

    JointPathPtr armPath = robot->getJointPath(robot->chestLink,
                                           robot->wristLink[arm]);
    for (int i=0; i<armPath->numJoints(); i++){
        Link *j = armPath->joint(i);
        PathEngine::Configuration::bounds(4+i, j->llimit, j->ulimit);
    }

    PathEngine::PathPlanner *planner = prob.planner();
    planner->setMobilityName("OmniWheel");
    planner->setAlgorithmName("RRT");
    PathEngine::RRT *rrt = (PathEngine::RRT *)planner->getAlgorithm();
    rrt->extendFromGoal(true);
    planner->getAlgorithm()->setProperty("eps", "0.2");
    planner->getAlgorithm()->setProperty("max-trials", "10000");
    planner->getAlgorithm()->setProperty("interpolation-distance", "0.2");
    prob.initCollisionCheckPairs();
    prob.initPlanner();

    for (unsigned int i=0; i<obstacles.size(); i++){
        Link *root = obstacles[i]->rootLink();
        root->p = obstacleP[i];
        root->R = rotFromRpy(obstacleRpy[i]);
        root->coldetModel->setPosition(root->R, root->p);
        obstacles[i]->calcForwardKinematics();
    }

    // set halfconf
    dvector halfconf;
    halfconf.setZero(robot->numJoints());
    halfconf[2] = halfconf[ 8] = ToRad(-34);
    halfconf[3] = halfconf[ 9] = ToRad( 66);
    halfconf[4] = halfconf[10] = ToRad(-32);
    double leg_link_len1=0, leg_link_len2=0; 
    halfconf[16] = halfconf[23] = ToRad(15);
    halfconf[17] = ToRad(-10); halfconf[24] = -halfconf[17];
    halfconf[19] = halfconf[26] = ToRad(-30);
    halfconf[20] = ToRad(80); halfconf[27] = -halfconf[20];
    halfconf[22] = halfconf[29] = -1.0;
    leg_link_len1 = leg_link_len2 = 0.3;
    double waistHeight = leg_link_len1*cos(halfconf[2])
        + leg_link_len2*cos(halfconf[4]) 
        + robot->FootToAnkle()[2];
    robot->rootLink()->p(2) = waistHeight;
    for (int i=0; i<robot->numJoints(); i++){
        robot->joint(i)->q = halfconf[i];
    }
    robot->calcForwardKinematics();

    myCfgSetter setter(robot, arm);

    PathEngine::Configuration startCfg;
    startCfg[0] = robot->rootLink()->p[2];
    startCfg[1] = startCfg[2] = startCfg[3] = 0;
    for (int i=0; i<armPath->numJoints(); i++){
        startCfg[4+i] = armPath->joint(i)->q;
    }
    planner->setStartConfiguration(startCfg);
    planner->setGoalConfiguration(goalCfg);
    planner->setApplyConfigFunc(boost::bind(&myCfgSetter::set, &setter,
                                            _1, _2));
    planner->setConfiguration(startCfg);
    prob.updateOLV();
    planner->setConfiguration(goalCfg);
    prob.updateOLV();
#if 0
    int earm;
    ifs >> earm;
    while (!ifs.eof()){
        PathEngine::Configuration cfg;
        for (unsigned int i=0; i<PathEngine::Configuration::size(); i++){
            ifs >> cfg[i];
        }
        if (arm == earm) {
            rrt->addExtraGoal(cfg);
            std::cout << "added an extra goal" << std::endl;
            planner->setConfiguration(cfg);
            prob.updateOLV();
        }
        ifs >> earm;
    }
#endif

    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    // plan
    bool ret = planner->calcPath();
    if (ret){
        for (int i=0; i<5; i++) {
            std::cout << "Shortcut:" << planner->getWayPoints().size() << "->";
            planner->optimize("Shortcut");
            std::cout << planner->getWayPoints().size() << std::endl;
        }
    }
    gettimeofday(&tv2, NULL);
    if (ret){
        const std::vector<PathEngine::Configuration>& postures 
            = planner->getWayPoints();
        for (unsigned int i=0; i<postures.size(); i++){
            planner->setConfiguration(postures[i]);
            prob.updateOLV();
        }
    }else{
        PathEngine::Roadmap *roadmap = planner->getRoadmap();
        for (unsigned int i=0; i<roadmap->nNodes(); i++){
            PathEngine::RoadmapNode *node = roadmap->node(i);
            planner->setConfiguration(node->position());
            prob.updateOLV();
        }
    }
    double time = (tv2.tv_sec - tv1.tv_sec) + ((double)(tv2.tv_usec - tv1.tv_usec))/1e6;
    std::cout << "total time = " << time << "[s]" << std::endl;
    double cdtime = prob.planner()->timeCollisionCheck();
    std::cout << "time spent in collision detection:"
              << cdtime << "[s](" << cdtime/time*100 << "[%]), "
              << cdtime*1e3/prob.planner()->countCollisionCheck() << "[ms/call]" << std::endl;
    std::cout << "profile:";
    setter.profile();

    return 0;
}
