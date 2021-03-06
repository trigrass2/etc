/*
  把持動作の探索
  把持の位置は点で、回転は範囲で与えられる
  環境は球の階層で、ロボットは球またはカプセルで近似した形状が干渉チェックに
  使用される
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
#include <hrpPlanner/RandomShortcutOptimizer.h>
#include <hrpPlanner/ShortcutOptimizer.h>
#include <hrpPlanner/ConfigurationSpace.h>
#include "problem.h"
#if 0
#include "myCfgSetter2.h"
#include "myCfgSetter3.h"
#else
#include "myCfgSetter2exactCOG.h"
#include "myCfgSetter3exactCOG.h"
#endif
#include "SphereTreeUtil.h"
#include "SphereTree.h"
#include <Math/Physics.h>
#include "CustomCD.h"
#include "RobotUtil.h"

using namespace motion_generator;
using namespace hrp;
using namespace PathEngine;

bool find_a_goal(problem &prob, myCfgSetter3 &setter, 
                 ConfigurationSpace& cspace, 
                 Configuration& goalCfg, JointPathPtr armPath[2])
{
    Configuration cfg = cspace.random();
    if (setter.set(prob.planner(), cfg) && !prob.planner()->checkCollision()){
        for (int i=0; i<4; i++) goalCfg[i] = cfg[i];
        int armDof = armPath[0]->numJoints();
        for (int i=0; i<2; i++){
            for (int j=0; j<armPath[i]->numJoints(); j++){
                goalCfg[4+i*armDof+j] = armPath[i]->joint(j)->q;
            }
        }
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    srand((unsigned)time(NULL));
 
    const char *robotURL = NULL;
    const char *goalURL = NULL;
    const char *cloudf="plant.pc";
    const char *initposf=NULL;
    std::vector<std::string> obstacleURL;
    std::vector<Vector3> obstacleP;
    std::vector<Vector3> obstacleRpy;
    Vector3 p, rpy;
    bool display = true;
    int ntrial = 1;
    double timeout = 3.0;
    for(int i = 1 ; i < argc; i++){
        if (strcmp(argv[i], "-robot") == 0){
            robotURL = argv[++i];
        }else if (strcmp(argv[i], "-obstacle") == 0){
            obstacleURL.push_back(argv[++i]);
            for (int j=0; j<3; j++) p[j] = atof(argv[++i]);
            obstacleP.push_back(p);
            for (int j=0; j<3; j++) rpy[j] = atof(argv[++i]);
            obstacleRpy.push_back(rpy);
        }else if (strcmp(argv[i], "-goal") == 0){
            goalURL = argv[++i];
        }else if (strcmp(argv[i], "-no-display")==0){
            display = false;
        }else if (strcmp(argv[i], "-ntrial")==0){
            ntrial = atoi(argv[++i]);
        }else if (strcmp(argv[i], "-init-pos")==0){
            initposf = argv[++i];
        }else if (strcmp(argv[i], "-timeout")==0){
            timeout = atof(argv[++i]);
        }
    }
    // goal position
    Vector3 goalP, goalRpy;
    for(int i = 1 ; i < argc; i++){
        if (strcmp(argv[i], "-goalpos") == 0){
            for (int j=0; j<3; j++) {
                goalP(j) = atof(argv[++i]);
            }
            for (int j=0; j<3; j++) {
                goalRpy(j) = atof(argv[++i]);
            }
        }
    }
    if (robotURL == NULL){
        std::cerr << "please specify URL of VRML model by -robot option"
                  << std::endl;
        return 1;
    }


    HumanoidBodyPtr robot = HumanoidBodyPtr(new HumanoidBody());
    loadHumanoidBodyFromModelLoader(robot, robotURL, argc, argv, true);
    JointPathPtr armPath[2];
    for (int k=0; k<2; k++){
        armPath[k] = robot->getJointPath(robot->chestLink,
                                         robot->wristLink[k]);
    }
    int armDof = armPath[0]->numJoints();

    problem prob(4+armDof*2);
    prob.addRobot("robot", robotURL, robot);
    std::vector<BodyPtr> obstacles;
    for (unsigned int i=0; i<obstacleURL.size(); i++){
        char buf[20];
        sprintf(buf, "obstacle%02d", i);
        obstacles.push_back(prob.addObstacle(buf, obstacleURL[i]));
    }
    BodyPtr goal;
    if (goalURL){
        goal = prob.addGoal("goal", goalURL);
    }

    // This must be called after all bodies are added
    if (display) prob.initOLV(argc, argv);

    PathPlanner *planner = prob.planner();
    planner->setMobilityName("OmniWheel");
    planner->setAlgorithmName("RRT");
    RRT *rrt = (RRT *)planner->getAlgorithm();
    rrt->extendFromGoal(true);
    rrt->epsilon(0.1);
    planner->getMobility()->interpolationDistance(0.05);
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
    setHalfConf(robot);
    if (initposf){
        std::ifstream ifs(initposf);
        if (!ifs.is_open()){
            std::cerr << "failed to open " << initposf << std::endl;
            return 1;
        }
        ifs >> robot;
        robot->calcForwardKinematics();
    }

    Matrix33 goalR(rotFromRpy(goalRpy));
    if (goal){
        goal->rootLink()->p = goalP;
        goal->rootLink()->R = goalR;
        goal->calcForwardKinematics();
    }

    myCfgSetter3 setterForGoal(robot, goalP);
    myCfgSetter2 setterForPath(robot);

    std::vector<hrp::Vector3> points;
    loadPointArray(cloudf, points);
    if (obstacleP.size() >= 2) addObstaclePoints(points, obstacleP[1]);
    SphereTree stree(obstacles[0]->rootLink(), points, 0.01);
    std::string shapeFile = robot->modelName() + ".shape";
    CustomCD cd(robot, shapeFile.c_str(), "hrp2.pairs", 
                obstacles[0], &stree);
    cd.tolerance(0.005);
    planner->setCollisionDetector(&cd);
    
    ConfigurationSpace* CSforPath = planner->getConfigurationSpace();

    CSforPath->bounds(0,  0.26, 0.705); // body z
    CSforPath->bounds(1, -0.5, 0.5); // body roll
    CSforPath->bounds(2, -0.0, 0.5); // body pitch
    CSforPath->bounds(3, -0.5, 0.5); // body yaw
    for (int k=0; k<2; k++){
        for (int i=0; i<armPath[k]->numJoints(); i++){
            Link *j = armPath[k]->joint(i);
            CSforPath->bounds(4+k*armDof+i, j->llimit, j->ulimit);
        }
    }

    CSforPath->weight(0) = 0.1; // z
    CSforPath->weight(1) = 1;  // roll
    CSforPath->weight(2) = 1;  // pitch
    CSforPath->weight(3) = 1;  // yaw

    CSforPath->weight(4) = 0.8;
    CSforPath->weight(5) = 0.6;
    CSforPath->weight(6) = 0.4;
    CSforPath->weight(7) = 0.3;
    CSforPath->weight(8) = 0.2;
    CSforPath->weight(9) = 0.1;

    CSforPath->weight(armDof+4) = 0.8;
    CSforPath->weight(armDof+5) = 0.6;
    CSforPath->weight(armDof+6) = 0.4;
    CSforPath->weight(armDof+7) = 0.3;
    CSforPath->weight(armDof+8) = 0.2;
    CSforPath->weight(armDof+9) = 0.1;

    if (armDof==7) CSforPath->weight(10) = CSforPath->weight(17) = 0.1;

    Configuration startCfg(CSforPath->size()), goalCfg(CSforPath->size());
    startCfg[0] = robot->rootLink()->p[2];
    rpy = rpyFromRot(robot->rootLink()->R);
    startCfg[1] = rpy[0]; startCfg[2] = rpy[1]; startCfg[3] = rpy[2];
    for (int j=0; j<2; j++){
        for (int i=0; i<armPath[j]->numJoints(); i++){
            startCfg[4+j*armDof+i] = armPath[j]->joint(i)->q;
        }
    }
    planner->setApplyConfigFunc(boost::bind(&myCfgSetter2::set, 
                                            &setterForPath, _1, _2));

    planner->setConfiguration(startCfg);
    //prob.updateOLV();
    if (planner->checkCollision()){
        std::cerr << "Error:start configuration is not collision-free" << std::endl;
        return 1;
    }

    int dim = armDof == 7 ? 8 : 7;
    ConfigurationSpace CSforGoal(dim); 
    CSforGoal.bounds(0,  0.26, 0.705); // body z
    CSforGoal.bounds(1, -0.5, 0.5); // body roll
    CSforGoal.bounds(2, -0.0, 0.5); // body pitch
    CSforGoal.bounds(3, -0.5, 0.5); // body yaw
    CSforGoal.bounds(4, -M_PI/2, M_PI/2);   // hand roll
    CSforGoal.bounds(5, -M_PI/2, M_PI/2); // hand pitch
    CSforGoal.bounds(6, -M_PI, M_PI);   // hand yaw
    if (dim == 8){
        CSforGoal.bounds(7, 
                         robot->wristLink[RIGHT]->llimit,
                         robot->wristLink[RIGHT]->ulimit);
    }

    double Psample = 0.1;
    RoadmapPtr Tg  = rrt->getBackwardTree();
    TimeMeasure tm;
    int ntimeout=0;
    struct timeval tv1, tv2;
    //
    std::cout << "#n time ngoal ncfg" << std::endl;
    for (int j=0; j<ntrial; j++){
        std::vector<RoadmapNodePtr> goals;
        std::vector<int> arms;
        rrt->getForwardTree()->clear();
        rrt->getBackwardTree()->clear();
        bool ret = false;
        int arm=-1;
        rrt->getForwardTree()->addNode(RoadmapNodePtr(new RoadmapNode(startCfg)));
        gettimeofday(&tv1, NULL);
        tm.begin();
        while(1){
            if (!Tg->nNodes() || rand() < Psample*RAND_MAX){
                if (find_a_goal(prob, setterForGoal, CSforGoal, goalCfg, armPath)){
                    //std::cout << "found a goal" << std::endl;
                    //prob.updateOLV();
                    RoadmapNodePtr goal = RoadmapNodePtr(new RoadmapNode(goalCfg));
                    Tg->addNode(goal);
                    goals.push_back(goal);
                    arms.push_back(setterForGoal.reachedArm());
                }
            }else{
                if (ret = rrt->extendOneStep()) {
                    // found a path
                    RoadmapNodePtr node = Tg->lastAddedNode();
                    while (node->nChildren()) node = node->child(0);
                    for (size_t i=0; i<goals.size(); i++){
                        if (goals[i] == node){
                            arm = arms[i];
                            break;
                        }
                    }
                    break;
                }
            }
            gettimeofday(&tv2, NULL);
            if (tv2.tv_sec - tv1.tv_sec + (tv2.tv_usec - tv1.tv_usec)/1e6
                > timeout) {
                ntimeout++;
                break;
            }
        }
        std::vector<Configuration> path;
        if (ret){
            rrt->extractPath(path);
            RandomShortcutOptimizer opt1(planner);
            ShortcutOptimizer       opt2(planner);
            for (int i=0; i<50; i++) {
                path = opt1.optimize(path);
                path = opt2.optimize(path);
            }
        }
        tm.end();
        if (ret){
            std::ofstream ofs("path.txt");
            ofs << arm << std::endl;

            for (unsigned int i=0; i<path.size(); i++){
                planner->setConfiguration(path[i]);
                if (display) prob.updateOLV();

                ofs << robot << std::endl;
            }
        }else{
            std::cout << "failed to find a path" << std::endl;
            std::cout << "nnodes = " << rrt->getForwardTree()->nNodes() << ","
                      << Tg->nNodes() << std::endl;
        }
        std::cout << j << " " << tm.time() << " " << goals.size() << " " 
                  << path.size() << std::endl;
    }
    double ttime = tm.totalTime();
    std::cout << "total   time = " << ttime << "[s]" << std::endl;
    std::cout << "average time = " << (ttime-timeout*ntimeout)/ntrial << "[s]" << std::endl;
    double cdtime = planner->timeCollisionCheck();
    std::cout << "time spent in collision detection:"
              << cdtime << "[s](" << cdtime/ttime*100 << "[%])" << std::endl;
    std::cout << "collision is checked " << planner->countCollisionCheck() << " times(" << cdtime/planner->countCollisionCheck()*1000 << "[ms/call])" << std::endl;
    std::cout << "profile of setter for goal:";
    setterForGoal.profile();
    std::cout << "profile of setter for path:";
    setterForPath.profile();

    return 0;
}
