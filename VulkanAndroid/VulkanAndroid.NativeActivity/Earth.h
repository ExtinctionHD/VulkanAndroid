#pragma once
#include "Model.h"
#include "TextureImage.h"

class Earth :
    public Model
{
public:
    enum TextureType
    {
        DAY,
        NIGHT,
        BUMP,
        SPECULAR,
        COUNT
    };

    Earth(Device *device, const std::string &texturePath);

    virtual ~Earth();

    std::vector<TextureImage*> getTextures() const;

private:
    const std::vector<std::string> TEXTURE_FILES{
        "Day.jpg", "Night.jpg", "Bump.jpg", "Specular.jpg"
    };

    std::vector<TextureImage*> textures;
};
