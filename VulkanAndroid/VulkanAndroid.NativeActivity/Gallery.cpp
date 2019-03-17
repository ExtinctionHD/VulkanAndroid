#include "Gallery.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include "sphere.h"
#include "utils.h"
#include "ActivityManager.h"

Gallery::Gallery(
    Device *device,
    const std::string &path,
    Earth *earth,
    Camera *camera,
    Controller *controller)
    : Model(device),
    earth(earth),
    camera(camera),
    controller(controller)
{
    loadPhotographs(device, path);

    parameterBuffer = new Buffer(device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(Parameters));
}

Gallery::~Gallery()
{
    delete parameterBuffer;
    delete texture;
}

TextureImage* Gallery::getTexture() const
{
    return texture;
}

Buffer* Gallery::getParameterBuffer() const
{
    return parameterBuffer;
}

void Gallery::update()
{
    const glm::vec2 cameraCoordinates = controller->getCoordinates(earth->getAngle());

    size_t index = 0;
    float nearestDistance = 360.0f;

    for (size_t i = 0; i < COORDINATES.size(); i++)
    {
        const float distance = loopDistance(cameraCoordinates, COORDINATES[i]);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            index = i;
        }
    }

    Parameters parameters{};

    if (nearestDistance < MAX_DISTANCE)
    {
        parameters.opacity = calculateOpacity(nearestDistance);

        setLocation(COORDINATES[index]);
    }

    parameters.index = float(index);
    parameterBuffer->updateData(&parameters);
}

void Gallery::loadPhotographs(Device *device, const std::string &path)
{
    std::vector<std::string> fileNames = ActivityManager::getFileNames(path, EXTENSIONS);

    std::vector<std::vector<uint8_t>> buffers;
    for (const auto &fileName : fileNames)
    {
        buffers.push_back(ActivityManager::read(fileName));
    }

    texture = new TextureImage( device, buffers, false, false);
    texture->pushFullView(VK_IMAGE_ASPECT_COLOR_BIT);
    texture->pushSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
}

float Gallery::loopDistance(glm::vec2 a, glm::vec2 b)
{
    float result;

    if (glm::abs(a.x - b.x) < 180.0f)
    {
        result = distance(a, b);
    }
    else
    {
        if (a.x < b.x)
        {
            a.x += 360.0f;
        }
        else
        {
            b.x += 360.0f;
        }
        result = distance(a, b);
    }

    return result;
}

float Gallery::calculateOpacity(float nearestDistance)
{
    const float minDistance = MAX_DISTANCE / 2.0f;

    float opacity = 1.0f - (nearestDistance - minDistance) / (MAX_DISTANCE - minDistance);
    opacity = opacity > 1.0f ? 1.0f : opacity;
    opacity = std::pow(opacity, 0.5f);

    return opacity;
}

void Gallery::setLocation(glm::vec2 photoCoordinates)
{
    const glm::vec3 position = sphere::R * axis::rotate(
        -axis::X,
        glm::vec2(photoCoordinates.x + earth->getAngle(), photoCoordinates.y),
        nullptr);

    const glm::vec3 direction = normalize(camera->getPosition() - position);
    const glm::vec2 angle(glm::radians(90.0f) + std::atan2(direction.z, direction.x), glm::asin(direction.y));

    glm::mat4 transformation = translate(glm::mat4(1.0f), position);
    transformation = rotate(transformation, angle.y, camera->getRight());
    transformation = rotate(transformation, angle.x, -axis::Y);
    transformation = scale(transformation, glm::vec3(5.0f, 2.8f, 1.0f));

    setTransformation(transformation);
}