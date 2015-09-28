#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <functional>
#include <memory>
#include <map>
#include <cctype>
#include <cassert>

#define GLEW_STATIC
#include <GL/glew.h>
#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

constexpr char *IMAGE_DIR = "res/images/";
constexpr char *MESH_DIR  = "res/models/";

template <typename T>
struct Result
{
    T obj;
    bool success = true;
    std::string error = "";
};

template<typename T>
Result<T> errorResult(std::string error)
{
    Result<T> result;
    result.success = false;
    result.error = error;
    return result;
}

template<typename T>
Result<T> successfulResult(T obj)
{
    Result<T> result;
    result.obj = std::move(obj);
    return result;
}

enum MeshLayout
{
    NONE = 0,
    POS = 1 << 0,
    TEX = 1 << 1,
    NORM = 1 << 2,
};

enum class MeshPrimitiveType { TRIANGLES, LINE_SEGMENTS };

struct MeshData
{
    std::vector<GLfloat> vertices;
    std::vector<GLuint> indices;
    char layout;
    MeshPrimitiveType primitive_type;
};

struct Mesh
{
    GLuint vao = 0, vbo = 0, ibo = 0;
    int num_vertices = 0;
    char layout = MeshLayout::NONE;

    Mesh(const MeshData& mesh_data);

    Mesh(const Mesh &other) = delete;
    Mesh& operator=(const Mesh &other) = delete;

    Mesh(Mesh &&other)
    {
        vao = other.vao;
        other.vao = 0;
    }

    Mesh &operator=(Mesh &&other)
    {
        vao = other.vao;
        other.vao = 0;
        return *this;
    }

    ~Mesh();
};

struct Shader
{
    GLuint id;
    GLuint vertex;
    GLuint fragment;

    GLint _model;
    GLint _view;
    GLint _projection;

    Shader() : id(0), vertex(0), fragment(0) {}

    Shader(const Shader &other) = delete;
    Shader& operator=(const Shader &other) = delete;

    Shader(Shader &&other)
    {
        moveHere(other);
    }

    Shader &operator=(Shader &&other)
    {
        moveHere(other);
        return *this;
    }

    ~Shader();

private: 

    void moveHere(Shader &other)
    {
        id = other.id;
        vertex = other.vertex;
        fragment = other.fragment;
        _model = other._model;
        _view = other._view;
        _projection = other._projection;
        other.id = 0;
        other.vertex = 0;
        other.fragment = 0;
    }
};

struct Image
{
    unsigned char *data;
    int width;
    int height;
    int channels;

    Image(const char *filename);
};

struct Texture
{
    GLuint id;

    Texture(const Image &image);

    Texture(const Texture &other) = delete;
    Texture& operator=(const Texture &other) = delete;

    Texture(Texture &&other)
    {
        id = other.id;
        other.id = 0;
    }

    Texture &operator=(Texture &&other)
    {
        id = other.id;
        other.id = 0;
        return *this;
    }

    ~Texture();
};

struct PathMesh
{
    MeshData data;
    std::vector<std::pair<int, int>> face_pairs;

    PathMesh(MeshData &mesh_data);
};
