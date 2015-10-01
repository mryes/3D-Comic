#include <comic.hpp>

static bool context_active = false;

bool isContextActive()
{
    return context_active;
}

using std::unique_ptr;
std::string loadFile(std::string fileName)
{
    auto fileStream = std::ifstream(fileName);
    std::stringstream string_stream;
    string_stream << fileStream.rdbuf();
    std::string contents = string_stream.str();
    return contents;
}

std::pair<std::string, std::string::iterator>
parseToken(std::string::iterator begin, std::string::iterator end)
{
    if (begin == end)
        return {"", end};
    auto token_begin = begin;
    while (token_begin != end && *token_begin == ' ')
        token_begin++;
    auto token_end = token_begin;
    while (token_end != end && *token_end != ' ')
        token_end++;
    std::string keyword = std::string(token_begin, token_end);
    return {keyword, token_end};
}

template <typename T, typename U>
bool contains(const std::map<T, U> &map, const T &key)
{
    return map.find(key) != map.end();
}

bool contains(const std::string &str, char to_find)
{
    return str.find(to_find) != std::string::npos;
}

template <typename T>
auto find(const std::vector<T> &v, const T& val)
{
    return std::find(v.begin(), v.end(), val);
}

void do_times(int times, std::function<void(int)> function)
{
    for (int i = 0; i < times; i++) function(i);
}

GLsizei vertexStride(const MeshData &mesh_data)
{
    if (mesh_data.layout == MeshLayout::NONE) return 0;
    assert(mesh_data.layout & MeshLayout::POS);
    GLsizei size = sizeof(GLfloat) * 3;
    if (mesh_data.layout & MeshLayout::TEX)  size += sizeof(GLfloat) * 2;
    if (mesh_data.layout & MeshLayout::NORM) size += sizeof(GLfloat) * 3;
    return size;
}

int indexStride(const MeshData &mesh_data)
{
    int stride = 1;
    if (mesh_data.layout & MeshLayout::TEX)  stride++;
    if (mesh_data.layout & MeshLayout::NORM) stride++;
    return stride;
}

const GLfloat *dataPointerFromVertexNumber(const MeshData &mesh_data, int vert_num)
{
    int vert_index = mesh_data.indices[vert_num] * vertexStride(mesh_data)/sizeof(GLfloat);
    return &mesh_data.vertices[vert_index];
}

Vertex getVertex(const MeshData &mesh_data, int vert_num)
{
    const GLfloat *component = dataPointerFromVertexNumber(mesh_data, vert_num);
    Vertex vertex;
    vertex.position = glm::vec3(component[0], component[1], component[2]);
    if (mesh_data.layout & MeshLayout::TEX)
        vertex.texcoord = glm::vec2(component[3], component[4]);
    if (mesh_data.layout & MeshLayout::NORM)
        vertex.normal = glm::vec3(component[5], component[6], component[7]);
    return vertex;
}

int verticesInFace(const MeshData &mesh_data)
{
    return mesh_data.primitive_type == MeshPrimitiveType::TRIANGLES ? 3 : 6;
}

glm::vec3 positionOnFace(const MeshData &mesh_data, int face, int rel_point)
{
    assert(rel_point < verticesInFace(mesh_data));
    int face_point = face * verticesInFace(mesh_data);
    int point = face_point + rel_point*2;
    return getVertex(mesh_data, point).position;
}

glm::vec3 centerPointOnFace(const MeshData &mesh_data, int face)
{
    int points_in_face = verticesInFace(mesh_data)/2;
    glm::vec3 sum {0};
    for (int i = 0; i < points_in_face; i++)
        sum += positionOnFace(mesh_data, face, i);
    return sum / static_cast<float>(points_in_face);
}

glm::vec3 normalOnFace(const MeshData &mesh_data, int face, int rel_point)
{
    assert(rel_point < verticesInFace(mesh_data));
    int face_point = face * verticesInFace(mesh_data);
    int point = face_point + rel_point*2;
    return getVertex(mesh_data, point).normal;
}

glm::vec3 averageNormalOnFace(const MeshData &mesh_data, int face)
{
    int points_in_face = verticesInFace(mesh_data)/2;
    glm::vec3 sum {0};
    for (int i = 0; i < points_in_face; i++)
        sum += normalOnFace(mesh_data, face, i);
    return sum / static_cast<float>(points_in_face);
}

const MeshData QUAD_MESH_DATA
{
    // Vertices
    {
         1,  1, 0,   1, 1,
         1, -1, 0,   1, 0,
        -1, -1, 0,   0, 0,
        -1,  1, 0,   0, 1
    },
    // Indices
    {
        0, 3, 1,
        1, 3, 2
    },

    POS | TEX
};

MeshData normalsMeshData(const MeshData &mesh_data)
{
    int number_of_faces = mesh_data.indices.size()/verticesInFace(mesh_data);
    MeshData normals_data;
    normals_data.primitive_type = MeshPrimitiveType::LINE_SEGMENTS;
    normals_data.layout = MeshLayout::POS;
    normals_data.vertices.reserve((3 + 3) * number_of_faces);
    std::vector<GLfloat> &data = normals_data.vertices;
    for (int f = 0; f < number_of_faces; f++)
    {
        glm::vec3 start = centerPointOnFace(mesh_data, f);
        glm::vec3 end = start + averageNormalOnFace(mesh_data, f);
        data.insert(data.end(), {start.x, start.y, start.z, end.x, end.y, end.z});
    }
    return normals_data;
}

Result<MeshData> loadOBJ(
    std::string obj_text_contents,
    MeshPrimitiveType load_mode = MeshPrimitiveType::TRIANGLES)
{
    // Parse the format X/X/X or X//X or X etc.
    auto indicesSplit = [](std::string param)
    {
        assert(!std::isspace(*param.begin()));
        std::array<int, 3> indices = { 0 };
        int number_of_indices = 0;
        auto loc = param.begin();
        do
        {
            auto num_begin = loc;
            auto num_end = loc;
            while (num_end != param.end() && std::isdigit(*num_end)) num_end++;
            if (num_end != param.end() && !contains("/ \t\n", *num_end)) break;
            if (num_begin != num_end) // index wasn't skipped here
            {
                int num;
                try { num = std::stoi(std::string(num_begin, num_end)); }
                catch (std::exception e)
                    { return errorResult<decltype(indices)>("Invalid index"); }
                // Number of indices hasn't been incremented yet.
                // So it's the correct index into the array at the moment.
                indices[number_of_indices] = num;
            }
            number_of_indices++;
            loc = num_end;
            if (*loc != '/') break; // nothing more to look at
            loc++;

        } while (number_of_indices < 3 && loc != param.end());

        return successfulResult(indices);
    };

    auto layoutFromIndices = [](const std::array<int, 3> &indices)
    {
        bool has_pos = indices[0] != 0;
        bool has_tex = indices[1] != 0;
        bool has_nor = indices[2] != 0;
        char layout = MeshLayout::NONE;
        if (!has_pos) return errorResult<char>("Missing position");
        layout |= MeshLayout::POS;
        if (has_tex) layout |= MeshLayout::TEX;
        if (has_nor) layout |= MeshLayout::NORM;
        return successfulResult(layout);
    };

    MeshData mesh;
    mesh.layout = MeshLayout::NONE;
    mesh.primitive_type = load_mode;

    std::vector<float> obj_positions;
    std::vector<float> obj_texcoords;
    std::vector<float> obj_normals;
    std::map<std::array<int, 3>, int> index_combos_seen_before;
    std::istringstream obj(obj_text_contents);
    std::string linebuf;
    std::string cur_material;
    int max_unused_index = 0;

    while (obj)
    {
        std::getline(obj, linebuf);
        auto loc = linebuf.begin();
        std::string keyword;
        std::tie(keyword, loc) = parseToken(loc, linebuf.end());
        if (keyword == "v" || keyword == "vt" || keyword == "vn")
        {
            int params_found = 0;
            while (loc != linebuf.end())
            {
                std::string param;
                tie(param, loc) = parseToken(loc, linebuf.end());
                if (param.empty()) continue;
                float value;
                try { value = std::stof(param); }
                catch (std::exception e) 
                    { return errorResult<MeshData>("OBJ parse error. Details: " + std::string(e.what())); }
                if (keyword == "v") obj_positions.push_back(value);
                else if (keyword == "vt") obj_texcoords.push_back(value);
                else if (keyword == "vn") obj_normals.push_back(value);
                params_found++;
            }
            if (keyword != "vt" && params_found != 3)
                return errorResult<MeshData>("Positions and normals need 3 parameters");
            else if (keyword == "vt" && params_found != 2)
                return errorResult<MeshData>("Texture coordinates need 2 parameters");
        }
        else if (keyword == "f")
        {
            int face_indices = 0;
            while (loc != linebuf.end())
            {
                std::string param;
                tie(param, loc) = parseToken(loc, linebuf.end());
                if (param.empty()) continue;
                std::array<int, 3> index_combo;
                {
                    auto index_combo_result = indicesSplit(param);
                    if (!index_combo_result.success)
                        return errorResult<MeshData>(
                            "Problem parsing indices " + param +
                            " on face; " + index_combo_result.error);
                    index_combo = index_combo_result.obj;
                }
                // mesh.layout
                {
                    auto index_layout_result = layoutFromIndices(index_combo);
                    if (!index_layout_result.success)
                        return errorResult<MeshData>(
                            "Problem parsing indices " + param +
                            " on face; " + index_layout_result.error);
                    if (mesh.layout != MeshLayout::NONE && mesh.layout != index_layout_result.obj)
                        return errorResult<MeshData>("Multiple index layouts confuse me");
                    mesh.layout = index_layout_result.obj;
                }
                if (!contains(index_combos_seen_before, index_combo))
                {
                    // We haven't seen this combination of v/vt/vn before,
                    // so we add the corresponding vertex values and assign a new index to them.
                    int v_idx  = index_combo[0] - 1; // obj indices start at 1
                    int vt_idx = index_combo[1] - 1;
                    int vn_idx = index_combo[2] - 1;
                    do_times(3, [&](int i) { mesh.vertices.push_back(obj_positions[3*v_idx + i]); });
                    if (index_combo[1])
                    {
                        mesh.vertices.push_back(obj_texcoords[2*vt_idx + 0]);
                        // Invert y to match opengl texture coordinates
                        mesh.vertices.push_back(1 - obj_texcoords[2*vt_idx + 1]);
                    }
                    if (index_combo[2])
                        do_times(3, [&](int i) { mesh.vertices.push_back(obj_normals[3*vn_idx + i]); });
                    index_combos_seen_before[index_combo] = max_unused_index++;
                }
                int cur_index = index_combos_seen_before[index_combo];
                face_indices++;
                if (load_mode == MeshPrimitiveType::TRIANGLES)
                {
                    if (face_indices <= 3) mesh.indices.push_back(cur_index);
                    else
                    {
                        // Need to continue triangle fan 
                        std::array<int, 3> new_tri;
                        new_tri[0] = *(mesh.indices.end() - 3);
                        new_tri[1] = *(mesh.indices.end() - 1);
                        new_tri[2] = cur_index;
                        for (int i : new_tri) mesh.indices.push_back(i);
                    }
                }
                else // line segments
                {
                    if (face_indices <= 2) mesh.indices.push_back(cur_index);
                    else 
                    {
                        mesh.indices.push_back(*(mesh.indices.end() - 1));
                        mesh.indices.push_back(cur_index);
                    }
                    if (loc == linebuf.end())
                    {
                        // Close polygon
                        mesh.indices.push_back(*(mesh.indices.end() - 1));
                        mesh.indices.push_back(*(mesh.indices.end() - (face_indices*2 - 1)));
                    }
                }
            }
            if (face_indices < 3)
                return errorResult<MeshData>("Faces must have at least 3 vertices");
        }
    }

    return successfulResult(std::move(mesh));
}

Mesh::Mesh(const MeshData &mesh_data)
{
    layout = mesh_data.layout;
    num_vertices = static_cast<int>(mesh_data.indices.size());
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(GLfloat) * mesh_data.vertices.size(),
        &mesh_data.vertices[0],
        GL_STATIC_DRAW);
    GLuint stride = vertexStride(mesh_data);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (char*)0);
    int top_attr_index = 1;
    if (mesh_data.layout & MeshLayout::TEX)
    {
        glEnableVertexAttribArray(top_attr_index);
        glVertexAttribPointer(top_attr_index, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3*sizeof(GLfloat)));
        top_attr_index++;
    }
    if (mesh_data.layout & MeshLayout::NORM)
    {
        glEnableVertexAttribArray(top_attr_index);
        glVertexAttribPointer(top_attr_index, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(5*sizeof(GLfloat)));
    }
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(GLuint) * mesh_data.indices.size(),
        &mesh_data.indices[0],
        GL_STATIC_DRAW);
    glBindVertexArray(0);
}

Mesh::~Mesh()
{
    if (!isContextActive()) return;
    std::cout << "What!\n";
    if (vao == 0 || vbo == 0 || ibo == 0) return;
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ibo);
}

void draw(const Mesh &mesh)
{
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.num_vertices), GL_UNSIGNED_INT, 0);
}

void bind(const Texture &texture)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
}

void draw(const Mesh &mesh, const Texture &texture)
{
    bind(texture);
    draw(mesh);
}

void quickPrintOpenGLError(int lineNum)
{
    GLenum error;
    while ((error = glGetError()) != GL_NO_ERROR)
        std::cerr << "(Line: " << lineNum << ") OpenGL error: " << gluErrorString(error) << "\n";
}

Result<Shader> makeShader(std::string vertex_src, std::string fragment_src)
{
    auto compileShaderPart = [](std::string src, GLenum type)
    {
        GLuint shaderPart = glCreateShader(type);
        const char *src_cstr = src.c_str();
        int length = static_cast<int>(src.size());
        glShaderSource(shaderPart, 1, &src_cstr, &length);
        assert(shaderPart > 0);
        glCompileShader(shaderPart);
        GLint status;
        glGetShaderiv(shaderPart, GL_COMPILE_STATUS, &status);
        if (status == GL_FALSE)
        {
            GLint logLength;
            glGetShaderiv(shaderPart, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> log(logLength);
            glGetShaderInfoLog(shaderPart, logLength, &logLength, &log[0]);
            return errorResult<GLuint>(std::string(log.begin(), log.end()));
        }
        return successfulResult(shaderPart);
    };

    Shader shader;
    Result<Shader> result;
    result.obj = Shader();
    {
        auto vert_result = compileShaderPart(vertex_src, GL_VERTEX_SHADER);
        auto frag_result = compileShaderPart(fragment_src, GL_FRAGMENT_SHADER);
        shader.vertex = vert_result.obj;
        shader.fragment = frag_result.obj;
        result.success = vert_result.success && frag_result.success;
        result.error =
            vert_result.error + (frag_result.success ? "" : "\n")
            + frag_result.error;
        if (!result.success) return result;
    }
    shader.id = glCreateProgram();
    glAttachShader(shader.id, shader.vertex);
    glAttachShader(shader.id, shader.fragment);
    glLinkProgram(shader.id);
    GLint status;
    glGetProgramiv(shader.id, GL_LINK_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint logLength = 0;
        glGetProgramiv(shader.id, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetProgramInfoLog(shader.id, logLength, &logLength, &log[0]);
        result.error  +=
            (result.success ? "" : "\n")
            + std::string(log.begin(), log.end());
        result.success = false;
        return result;
    }

    result.obj = std::move(shader);
    return result;
}

void use(const Shader &shader)
{
    glUseProgram(shader.id);
}

void initTransformationMatrices(Shader &shader)
{
    shader._model = glGetUniformLocation(shader.id, "model");
    shader._view = glGetUniformLocation(shader.id, "view");
    shader._projection = glGetUniformLocation(shader.id, "projection");
}

void setModelTransform(const Shader &shader, glm::mat4 &mat)
{
    use(shader);
    glUniformMatrix4fv(shader._model, 1, GL_FALSE, glm::value_ptr(mat));
}

void setCameraTransform(const Shader &shader, glm::mat4 &mat)
{
    use(shader);
    glUniformMatrix4fv(shader._view, 1, GL_FALSE, glm::value_ptr(mat));
}

void setProjectionTransform(const Shader &shader, glm::mat4 mat)
{
    use(shader);
    glUniformMatrix4fv(shader._projection, 1, GL_FALSE, glm::value_ptr(mat));
}

void setColorUniform(const Shader &shader, const char *name, glm::vec3 color)
{
    GLint loc = glGetUniformLocation(shader.id, name);
    use(shader);
    glUniform3fv(loc, 1, glm::value_ptr(color));
}

void setHasTexture(Shader &shader)
{
    GLint texture_loc = glGetUniformLocation(shader.id, "tex");
    use(shader);
    glUniform1i(texture_loc, 0);
}

Shader::~Shader()
{
    if (!isContextActive()) return;
    if (!id) return;
    glDetachShader(id, vertex);
    glDetachShader(id, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    glDeleteProgram(id);
}

Image::Image(const char *filename)
{
    std::string full_path = IMAGE_DIR;
    full_path += filename;
    data = stbi_load(full_path.data(), &width, &height, &channels, 4);
}

Texture::Texture(const Image &image)
{
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLenum format = image.channels == 4 ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, image.width, image.height, 0, format, GL_UNSIGNED_BYTE, image.data);
}

Texture::~Texture()
{
    if (!isContextActive()) return;
    if (!id) return;
    glDeleteTextures(1, &id);
}

PathMesh::PathMesh(MeshData &mesh_data)
{
    assert(mesh_data.primitive_type == MeshPrimitiveType::LINE_SEGMENTS);
    data = std::move(mesh_data);
    face_pairs.reserve(mesh_data.indices.size()/2);
    // Assumption: faces are triangles.
    // In the future this could be a parameter.
    int face_sides = 3;
    int indices_per_face = 2*face_sides;
    for (int i = 0; i < mesh_data.indices.size(); i += indices_per_face)
    {
        int face_index = i/indices_per_face;
        int line_beginning_index = i/face_sides;
        for (int line = line_beginning_index; i < face_sides; i++)
        {
            if (face_pairs[line].first == 0)
                face_pairs[line].first = face_index;
            else if (face_pairs[line].second == 0)
                face_pairs[line].second = face_index;
            else assert(false); // three faces sharing a line? not possible.
        };
    }
}

void offset(PathMesh &path_mesh, glm::vec3 amount)
{
    auto &data = path_mesh.data.vertices;
    auto stride = vertexStride(path_mesh.data);
    for (int i = 0; i < data.size(); i += stride)
    {
        data[i+0] += amount.x;
        data[i+1] += amount.y;
        data[i+2] += amount.z;
    }
}

int main(int argc, char *argv[])
{
    auto parseObjResult = loadOBJ(loadFile("res/models/just_pyramid_ball.obj"));
    if (!parseObjResult.success)
    {
        std::cerr << parseObjResult.error << "\n";
        return EXIT_FAILURE;
    }
    MeshData model_mesh_data = parseObjResult.obj;

    parseObjResult = loadOBJ(loadFile("res/models/path.obj"), MeshPrimitiveType::LINE_SEGMENTS);
    if (!parseObjResult.success)
    {
        std::cerr << parseObjResult.error << "\n";
        return EXIT_FAILURE;
    }
    PathMesh path_mesh {parseObjResult.obj};
    MeshData normals_mesh_data = normalsMeshData(path_mesh.data);

    Image model_texture_image {"chinese_box.gif"};
    Image floor_texture_image {"slimy_vines.png"};

    int screen_width = 1600; 
    int screen_height = 900;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_DisplayMode display_mode;
    SDL_GetCurrentDisplayMode(0, &display_mode);
    // screen_width = display_mode.w;
    // screen_height = display_mode.h;
    SDL_Window *window = SDL_CreateWindow(
        "Lego Island", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        screen_width, screen_height, SDL_WINDOW_OPENGL);
    if (!window)
    {
        std::cerr << "SDL error: " << SDL_GetError() << "\n";
        return EXIT_FAILURE;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context)
    {
        std::cerr << "SDL error: " << SDL_GetError() << "\n";
        return EXIT_FAILURE;
    }
    context_active = true;
    SDL_ShowCursor(0);

    glewExperimental = GL_TRUE;
    GLenum initStatus = glewInit();
    if (initStatus != GLEW_OK)
    {
        std::cerr << "Glew error: " << glewGetErrorString(initStatus) << "\n";
        return EXIT_FAILURE;
    }
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
        std::cerr << "OpenGL error: " << gluErrorString(error) << "\n";

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    Shader shader;
    {
        auto shaderResult = makeShader(
            loadFile("res/shaders/test.vert"),
            loadFile("res/shaders/test.frag"));
        if (!shaderResult.success)
        {
            std::cerr << "Shader error: " << shaderResult.error << "\n";
            return EXIT_FAILURE;
        }
        shader = std::move(shaderResult.obj);
    }

    initTransformationMatrices(shader);
    setColorUniform(shader, "background_color", glm::vec3(1.f, 0.2f, 0.f));
    setHasTexture(shader);

    Mesh model_mesh {model_mesh_data};
    Texture model_texture {model_texture_image};

    Mesh floor_mesh {QUAD_MESH_DATA};
    Texture floor_texture {floor_texture_image};

    Mesh path_display_mesh {path_mesh.data};

    Mesh normals_mesh {normals_mesh_data};

    glm::vec3 model_pos {0.f, 1.f, -1.f};
    float model_rotation = 0, model_rotation_speed = -1.f;

    glm::vec3 eye_pos = positionOnFace(path_mesh.data, 20, 4);
    glm::vec2 eye_vel {0.f, 0.f};
    glm::vec3 eye_look_direction {0.f, 0.f, -1.f};
    glm::vec3 eye_raised {0.f, 0.2f, 0.f};
    float eye_pitch = 0, eye_yaw = 90;
    float eye_speed = 0.5f;

    bool w_down = false, a_down = false, s_down = false, d_down = false;
    int prev_mouse_x = screen_width/2, prev_mouse_y = screen_height/2;
    float previous_time = SDL_GetTicks() / 1000.f;
    float dt = 0;
    bool wireframe = false;
    bool running = true;
    SDL_Event event;

    while (running)
    {
        float now = SDL_GetTicks() / 1000.f;
        dt = now - previous_time;
        previous_time = now;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT) running = false;
            else if (event.type == SDL_MOUSEMOTION)
            {
                int mouse_movement_x = event.motion.x - prev_mouse_x;
                int mouse_movement_y = event.motion.y - prev_mouse_y;
                prev_mouse_x = event.motion.x;
                prev_mouse_y = event.motion.y;
                eye_yaw += mouse_movement_x/4.f;
                eye_pitch += -mouse_movement_y/6.f;
                if (eye_pitch > 89.f)  eye_pitch = 89.f;
                if (eye_pitch < -89.f) eye_pitch = -89.f;
                eye_look_direction.x =
                    glm::cos(glm::radians(eye_pitch))
                    * glm::cos(glm::radians(eye_yaw));
                eye_look_direction.y = glm::sin(glm::radians(eye_pitch));
                eye_look_direction.z =
                    glm::cos(glm::radians(eye_pitch))
                    * glm::sin(glm::radians(eye_yaw));
                eye_look_direction = glm::normalize(eye_look_direction);
            }
            else if (event.type == SDL_KEYDOWN)
            {
                auto key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) running = false;
                else if (key == SDLK_m) wireframe = !wireframe;
                else if (key == SDLK_w) w_down = true;
                else if (key == SDLK_a) a_down = true;
                else if (key == SDLK_s) s_down = true;
                else if (key == SDLK_d) d_down = true;
            }
            else if (event.type == SDL_KEYUP)
            {
                auto key = event.key.keysym.sym;
                if      (key == SDLK_w) w_down = false;
                else if (key == SDLK_a) a_down = false;
                else if (key == SDLK_s) s_down = false;
                else if (key == SDLK_d) d_down = false;
            }
        }

        model_rotation += model_rotation_speed * dt;

        // Eye control 
        {
            glm::vec2 eye_accel {0.f, 0.f};
            if (w_down) eye_accel.y =  1;
            if (a_down) eye_accel.x = -1;
            if (s_down) eye_accel.y = -1;
            if (d_down) eye_accel.x =  1;
            if (eye_accel != glm::zero<glm::vec2>())
                eye_accel = glm::normalize(eye_accel) * eye_speed;
            glm::vec3 eye_forward = eye_look_direction;
            eye_forward.y = 0;
            eye_forward = glm::normalize(eye_forward);
            eye_accel += -6.0f*eye_vel;
            eye_vel += eye_accel*dt;
            glm::vec3 world_space_movement =
                eye_vel.y * eye_forward +
                eye_vel.x * glm::normalize(glm::cross(eye_forward, glm::vec3(0.f, 1.f, 0.f)));
            eye_pos += world_space_movement;
        }

        glClearColor(0.9f, 0.9f, 0.9f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLineWidth(2);
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        use(shader);
        setCameraTransform(shader, glm::lookAt(eye_pos + eye_raised, eye_pos + eye_raised + eye_look_direction, glm::vec3(0.f, 1.f, 0.f)));
        setProjectionTransform(shader, glm::perspective(45.f, (float)screen_width / screen_height, 0.1f, 100.f));

        glm::mat4 model = glm::translate(glm::mat4(1.0f), model_pos);
        model = glm::scale(model, glm::vec3(3.f, 3.f, 3.f));
        // model = glm::rotate(model, model_rotation, glm::vec3(0.f, 1.f, 0.f));
        setModelTransform(shader, model);
        glEnable(GL_CULL_FACE);
        draw(model_mesh, model_texture);

        setModelTransform(shader, glm::mat4(1.0f));
        glDisable(GL_CULL_FACE);
        bind(floor_texture);
        glBindVertexArray(path_display_mesh.vao);
        glDrawElements(GL_LINES, static_cast<GLsizei>(path_display_mesh.num_vertices), GL_UNSIGNED_INT, 0);

        glBindVertexArray(normals_mesh.vao);
        glDrawArrays(GL_LINES, 0, normals_mesh_data.vertices.size()/2);

        SDL_GL_SwapWindow(window);

        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            std::cerr << "OpenGL error: " << gluErrorString(error) << "\n";
            break;
        }
    }

    context_active = false;
    SDL_GL_DeleteContext(context);
    SDL_Quit();
    return 0;
}
