#include "glew.h"
#include "freeglut.h"
#include "glm.hpp"
#include "ext.hpp"
#include <iostream>
#include <cmath>
#include <vector>

#include "Shader_Loader.h"
#include "Render_Utils.h"
#include "Camera.h"
#include "Texture.h"
#include "Physics.h"

using namespace std;

namespace Objects {
    struct Properties {
        glm::vec3 size, pos;
    };

    const int numPins = 10;

	Properties  ball = { {0.6, 0.6, 0.6 }, {0.75, 0.25, 6} },
		pins[numPins] = {
		{ { 1.5, 1.5, 1.0 },{3.0, 0.5, -6.0} }, //dziesi�ta
		{ {1.5, 1.5, 1.0},{1.5, 0.5, -6.0} }, //dziewi�ta
		{ {1.5, 1.5, 1.0},{0.0, 0.5, -6.0} },  //�sma
		{ {1.5, 1.5, 1.0},{-1.5, 0.5, -6.0} }, //si�dma
		{ {1.5, 1.5, 1.0},{2.25, 0.5, -4.0} }, //sz�sta
		{ {1.5, 1.5, 1.0},{0.75,0.5, -4.0} },  //pi�ta
		{ {1.5, 1.5, 1.0},{-0.75, 0.5, -4.0} }, //czwarta
		{ {1.5, 1.5, 1.0}, {1.5,0.5, -2.0} },   //trzecia
		{ {1.5, 1.5, 1.0}, {0.0, 0.5, -2.0} },  //druga
		{ { 1.5, 1.5, 1.0 },{0.75, 0.5, 0.0} } };//pierwsza 
				
}

Core::Shader_Loader shaderLoader;
GLuint programColor;
GLuint programTexture;

obj::Model planeModel, ballModel, pinModel;
GLuint ballTexture, groundTexture, pinTexture;

glm::vec3 cameraPos = glm::vec3(0, 2, 10);
glm::vec3 cameraDir;
glm::vec3 cameraSide;
float cameraAngle = 0;
glm::mat4 cameraMatrix, perspectiveMatrix;

glm::vec3 lightDir = glm::normalize(glm::vec3(0.5, -1, -0.5));

Physics pxScene(9.8 /* gravity (m/s^2) */);

const double physicsStepTime = 1.f / 60.f;
double physicsTimeToProcess = 0;

PxMaterial      *material = nullptr;
PxRigidStatic   *bodyGround = nullptr;
PxRigidDynamic	* bodyBall = nullptr,
				* bodyPins[Objects::numPins] = { nullptr, nullptr, nullptr };
                
struct Renderable {
    obj::Model *model;
    glm::mat4 localTransform, physicsTransform;
    GLuint textureId;
};
Renderable rendGround,
    rendHandle, rendPins[10], rendBox, rendBall;
vector<Renderable*> renderables;


void initRenderables()
{
    planeModel = obj::loadModelFromFile("models/plane.obj");
    pinModel = obj::loadModelFromFile("models/pin2.obj");
	ballModel = obj::loadModelFromFile("models/sphere.obj");

    groundTexture = Core::LoadTexture("textures/wood.jpg");
    ballTexture = Core::LoadTexture("textures/red_ball.jpg");
	pinTexture = Core::LoadTexture("textures/white2.jpg");

    rendGround.model = &planeModel;
    rendGround.textureId = groundTexture;
    renderables.emplace_back(&rendGround);

	rendBall.model = &ballModel;   //create ball
	rendBall.textureId = ballTexture;
	rendBall.localTransform = glm::scale(Objects::ball.size * 0.5f);
	renderables.emplace_back(&rendBall);

    for (int i = 0; i < Objects::numPins; i++) {   //create pins
        rendPins[i].model = &pinModel;
        rendPins[i].textureId = pinTexture;
        rendPins[i].localTransform = glm::scale(Objects::pins[i].size * 0.5f);
        renderables.emplace_back(&rendPins[i]);
    }

}

PxTransform posToPxTransform(glm::vec3 const& pos) {
    return PxTransform(pos.x, pos.y, pos.z);
}
// helper function: glm::vec3 size -> PxBoxGeometry
PxBoxGeometry sizeToPxBoxGeometry(glm::vec3 const& size) {
    auto h = size * 0.5f;
    return PxBoxGeometry(h.x, h.y, h.z);
}

void createDynamicBox(PxRigidDynamic* &body, Renderable *rend, glm::vec3 const& pos, glm::vec3 const& size)
{
    body = pxScene.physics->createRigidDynamic(posToPxTransform(pos));
    PxShape* boxShape = pxScene.physics->createShape(sizeToPxBoxGeometry(size), *material);
    body->attachShape(*boxShape);
    boxShape->release();
    body->userData = rend;
    pxScene.scene->addActor(*body);
}

void createDynamicSphere(PxRigidDynamic* &body, Renderable *rend, glm::vec3 const& pos, float radius)
{
    body = pxScene.physics->createRigidDynamic(posToPxTransform(pos));
    PxShape* sphereShape = pxScene.physics->createShape(PxSphereGeometry(radius), *material);
    body->attachShape(*sphereShape);
    sphereShape->release();
    body->userData = rend;
    pxScene.scene->addActor(*body);
}

void initPhysicsScene()
{
    material = pxScene.physics->createMaterial(0.5f, 0.5f, 0.6f);

    bodyGround = pxScene.physics->createRigidStatic(PxTransformFromPlaneEquation(PxPlane(0, 1, 0, 0)));
    PxShape* planeShape = pxScene.physics->createShape(PxPlaneGeometry(), *material);
    bodyGround->attachShape(*planeShape);
    planeShape->release();
    bodyGround->userData = &rendGround;
    pxScene.scene->addActor(*bodyGround);

    for (int i = 0; i < Objects::numPins; i++) {
        createDynamicBox(bodyPins[i], &rendPins[i], Objects::pins[i].pos, Objects::pins->size);
        PxRigidBodyExt::setMassAndUpdateInertia(*bodyPins[i], 0.05f);
    }

	createDynamicSphere(bodyBall, &rendBall, Objects::ball.pos, Objects::ball.size.x);
	PxRigidBodyExt::setMassAndUpdateInertia(*bodyBall, 1.f);
}

void updateTransforms()
{
    //retrieve the current transforms of the objects from the physical simulation
    auto actorFlags = PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC;
    PxU32 nbActors = pxScene.scene->getNbActors(actorFlags);
    if (nbActors)
    {
        vector<PxRigidActor*> actors(nbActors);
        pxScene.scene->getActors(actorFlags, (PxActor**)&actors[0], nbActors);
        for (auto actor : actors)
        {
            // use the userData of the objects to set up the model matrices of proper renderables
            if (!actor->userData) continue;
            Renderable *renderable = (Renderable*)actor->userData;

            // get world matrix of the object (actor)
            PxMat44 transform = actor->getGlobalPose();
            auto &c0 = transform.column0;
            auto &c1 = transform.column1;
            auto &c2 = transform.column2;
            auto &c3 = transform.column3;

            //model matrix used for the rendering
            renderable->physicsTransform = glm::mat4(
                c0.x, c0.y, c0.z, c0.w,
                c1.x, c1.y, c1.z, c1.w,
                c2.x, c2.y, c2.z, c2.w,
                c3.x, c3.y, c3.z, c3.w);
        }
    }
}

void moveBall(float offset) {
	if (!bodyBall) return;
	Objects::ball.pos.x += offset;
	bodyBall->setGlobalPose(posToPxTransform(Objects::ball.pos));
	Objects::ball.pos.y += offset;
	bodyBall->setGlobalPose(posToPxTransform(Objects::ball.pos));
	for (int i = 0; i < Objects::numPins; i++)
		bodyPins[i]->wakeUp();
}

void shootBall() {
	if (!bodyBall) return;
	bodyBall->setLinearVelocity(PxVec3(0.f, 0.f, -17.0f));

}

void keyboard(unsigned char key, int x, int y)
{
    float angleSpeed = 0.1f;
    float moveSpeed = 0.1f;
    float ballSpeed = 0.05f;
    switch (key)
    {
    case 'z': cameraAngle -= angleSpeed; break;
    case 'x': cameraAngle += angleSpeed; break;
    case 'w': cameraPos += cameraDir * moveSpeed; break;
    case 's': cameraPos -= cameraDir * moveSpeed; break;
    case 'd': cameraPos += cameraSide * moveSpeed; break;
    case 'a': cameraPos -= cameraSide * moveSpeed; break;

    case 'j': moveBall(-ballSpeed);  break;
	case 'l': moveBall(ballSpeed);  break;
	case 'i': shootBall(); break;
	
    }
}

void mouse(int x, int y)
{
}

glm::mat4 createCameraMatrix()
{
    cameraDir = glm::normalize(glm::vec3(cosf(cameraAngle - glm::radians(90.0f)), 0, sinf(cameraAngle - glm::radians(90.0f))));
    glm::vec3 up = glm::vec3(0, 1, 0);
    cameraSide = glm::cross(cameraDir, up);

    return Core::createViewMatrix(cameraPos, cameraDir, up);
}

void drawObjectColor(obj::Model * model, glm::mat4 modelMatrix, glm::vec3 color)
{
    GLuint program = programColor;

    glUseProgram(program);

    glUniform3f(glGetUniformLocation(program, "objectColor"), color.x, color.y, color.z);
    glUniform3f(glGetUniformLocation(program, "lightDir"), lightDir.x, lightDir.y, lightDir.z);

    glm::mat4 transformation = perspectiveMatrix * cameraMatrix * modelMatrix;
    glUniformMatrix4fv(glGetUniformLocation(program, "modelViewProjectionMatrix"), 1, GL_FALSE, (float*)&transformation);
    glUniformMatrix4fv(glGetUniformLocation(program, "modelMatrix"), 1, GL_FALSE, (float*)&modelMatrix);

    Core::DrawModel(model);

    glUseProgram(0);
}

void drawObjectTexture(obj::Model * model, glm::mat4 modelMatrix, GLuint textureId)
{
    GLuint program = programTexture;

    glUseProgram(program);

    glUniform3f(glGetUniformLocation(program, "lightDir"), lightDir.x, lightDir.y, lightDir.z);
    Core::SetActiveTexture(textureId, "textureSampler", program, 0);

    glm::mat4 transformation = perspectiveMatrix * cameraMatrix * modelMatrix;
    glUniformMatrix4fv(glGetUniformLocation(program, "modelViewProjectionMatrix"), 1, GL_FALSE, (float*)&transformation);
    glUniformMatrix4fv(glGetUniformLocation(program, "modelMatrix"), 1, GL_FALSE, (float*)&modelMatrix);

    Core::DrawModel(model);

    glUseProgram(0);
}

void renderScene()
{
    double time = glutGet(GLUT_ELAPSED_TIME) / 1000.0;
    static double prevTime = time;
    double dtime = time - prevTime;
    prevTime = time;

    if (dtime < 1.f) {
        physicsTimeToProcess += dtime;
        while (physicsTimeToProcess > 0) {
            pxScene.step(physicsStepTime);
            physicsTimeToProcess -= physicsStepTime;
        }
    }

    cameraMatrix = createCameraMatrix();
    perspectiveMatrix = Core::createPerspectiveMatrix();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(0.0f, 0.1f, 0.3f, 1.0f);

    updateTransforms();

    for (Renderable* renderable : renderables) {
        drawObjectTexture(renderable->model, renderable->physicsTransform * renderable->localTransform, renderable->textureId);
    }

    glutSwapBuffers();
}

void init()
{
    srand(time(0));
    glEnable(GL_DEPTH_TEST);
    programColor = shaderLoader.CreateProgram("shaders/shader_color.vert", "shaders/shader_color.frag");
    programTexture = shaderLoader.CreateProgram("shaders/shader_tex.vert", "shaders/shader_tex.frag");

    initRenderables();
    initPhysicsScene();
}

void shutdown()
{
    shaderLoader.DeleteProgram(programColor);
    shaderLoader.DeleteProgram(programTexture);
}

void idle()
{
    glutPostRedisplay();
}

int main(int argc, char ** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowPosition(200, 200);
    glutInitWindowSize(600, 600);
    glutCreateWindow("OpenGL + PhysX");
    glewInit();

    init();
    glutKeyboardFunc(keyboard);
    glutPassiveMotionFunc(mouse);
    glutDisplayFunc(renderScene);
    glutIdleFunc(idle);

    glutMainLoop();

    shutdown();

    return 0;
}
