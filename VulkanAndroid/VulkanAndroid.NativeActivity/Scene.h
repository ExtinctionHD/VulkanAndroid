#pragma once
#include "Camera.h"
#include "Earth.h"

class Scene
{
public:
    static const uint32_t BUFFER_COUNT = 2;
    static const uint32_t TEXTURE_COUNT = Earth::TextureType::COUNT;

    Scene(Device *device, VkExtent2D extent);

    ~Scene();

    Buffer* getCameraBuffer() const;

    Buffer* getEarthTransformationBuffer() const;

    std::vector<TextureImage*> getEarthTextures() const;

    void resize(VkExtent2D newExtent);

    void drawSphere(VkCommandBuffer commandBuffer) const;

private:
    Camera *camera;

    Buffer *sphereVertexBuffer;

    Buffer *sphereIndexBuffer;

    uint32_t indexCount;

    Earth *earth;
};

