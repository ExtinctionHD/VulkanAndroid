#pragma once
#include "Model.h"
#include "TextureImage.h"
#include "Earth.h"
#include "Camera.h"
#include "Controller.h"

class Gallery : public Model
{
public:
    Gallery(Device *device, const std::string &path, Earth *earth, Camera *camera, Controller *controller);

    virtual ~Gallery();

    TextureImage* getTexture() const;

    Buffer* getParameterBuffer() const;

    void update();

private:
    struct Parameters
    {
        float index;
        float opacity;
    };

    const std::vector<glm::vec2> COORDINATES{
        glm::vec2(2.293966f, 48.858187f),
        glm::vec2(151.215297, -33.856829f),
    };

    const float DISTANCE_LIMIT_FACTOR = 1.25;

    const float SCALE_FACTOR = 0.4f;

    Buffer *parameterBuffer;

    Earth *earth;

    Camera *camera;

    Controller *controller;

    TextureImage *texture;

    void loadPhotographs(Device *device, const std::string &path);

    float loopDistance(glm::vec2 a, glm::vec2 b);

    float calculateNearestDistance(glm::vec2 cameraCoordinates, uint32_t *outIndex);

    float calculateOpacity(float nearestDistance, float distanceLimit);

    glm::vec3 calculateScale();

    glm::mat4 calculateTransformation(glm::vec2 photoCoordinates, glm::vec2 cameraCoordinates);
};
