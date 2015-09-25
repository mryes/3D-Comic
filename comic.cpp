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

GLsizei vertexStride(MeshData& mesh)
{
    if (mesh.layout == MeshLayout::NONE) return 0;
    assert(mesh.layout & MeshLayout::POS);
    GLsizei size = sizeof(GLfloat) * 3;
    if (mesh.layout & MeshLayout::TEX)  size += sizeof(GLfloat) * 2;
    if (mesh.layout & MeshLayout::NORM) size += sizeof(GLfloat) * 3;
    return size;
}

Mesh::Mesh(MeshData &mesh_data)
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

Result<MeshData> loadOBJ(std::string obj_text_contents)
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
        else if (keyword == "usematl")
        {
            std::string param;
            tie(param, loc) = parseToken(loc, linebuf.end());
            if (param.empty()) continue;
            mesh.materials.push_back(param);
            cur_material = param;
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
            if (face_indices < 3)
                return errorResult<MeshData>("Faces must have at least 3 vertices");
            // Material index is just index into material vector
            mesh.material_indices.push_back(
                static_cast<int>(
                    find(mesh.materials, cur_material)
                    - mesh.materials.begin()));
        }
    }

    return successfulResult(std::move(mesh));
}

void draw(Mesh &mesh)
{
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.num_vertices), GL_UNSIGNED_INT, 0);
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

void initTransformationMatrices(Shader &shader)
{
    shader._model = glGetUniformLocation(shader.id, "model");
    shader._view = glGetUniformLocation(shader.id, "view");
    shader._projection = glGetUniformLocation(shader.id, "projection");
}

void setModelTransform(Shader &shader, glm::mat4 &mat)
{
    glUniformMatrix4fv(shader._model, 1, GL_FALSE, glm::value_ptr(mat));
}

void setCameraTransform(Shader &shader, glm::mat4 &mat)
{
    glUniformMatrix4fv(shader._view, 1, GL_FALSE, glm::value_ptr(mat));
}

void setProjectionTransform(Shader &shader, glm::mat4 mat)
{
    glUniformMatrix4fv(shader._projection, 1, GL_FALSE, glm::value_ptr(mat));
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
    int img_channels;
    std::string full_path = IMAGE_DIR;
    full_path += filename;
    data = stbi_load(full_path.data(), &width, &height, &img_channels, 3);
}

Texture::Texture(Image &image)
{
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data);
}

Texture::~Texture()
{
    if (!isContextActive()) return;
    if (!id) return;
    glDeleteTextures(1, &id);
}

void debugPrintMesh(MeshData &mesh)
{
    int stride = vertexStride(mesh)/sizeof(GLfloat);
    int cur = 0;
    for (auto v : mesh.vertices)
    {
        std::cout << v << " ";
        cur++;
        if (cur == stride)
        {
            std::cout << " | ";
            cur = 0;
        }
    }
    std::cout << "\n";
    for (auto i : mesh.indices)
        std::cout << i << " ";
    std::cout << "\n";
}

int main(int argc, char *argv[])
{
    auto parseObjResult = loadOBJ(loadFile("res/models/skull.obj"));
    if (!parseObjResult.success)
    {
        std::cerr << parseObjResult.error << "\n";
        return EXIT_FAILURE;
    }
    MeshData mesh_data = parseObjResult.obj;

    auto image = Image{"skull.png"};

    int screen_width = 1200; 
    int screen_height = 800;

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
    // glEnable(GL_CULL_FACE);

    Shader shader;
    {
        auto shaderResult = makeShader(loadFile("res/shaders/test.vert"), loadFile("res/shaders/test.frag"));
        if (!shaderResult.success)
        {
            std::cerr << "Shader error: " << shaderResult.error << "\n";
            return EXIT_FAILURE;
        }
        shader = std::move(shaderResult.obj);
    }
    initTransformationMatrices(shader);
    GLint texture_loc = glGetUniformLocation(shader.id, "tex");

    Mesh mesh {mesh_data};

    auto texture = Texture{image};

    float rotation = 0;
    float rotation_speed = 2.f;
    float z_position = -5;
    float x_position = 0;
    float previous_time = SDL_GetTicks() / 1000.f;
    float dt = 0;
    bool wireframe = false;
    SDL_Event event;
    while (true)
    {
        if (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT) break;
            else if (event.type == SDL_MOUSEMOTION)
            {
                z_position = ((float)event.motion.y / screen_height) * -90;
                x_position = (event.motion.x - screen_width/2) / 50.f;
            }
            else if (event.type == SDL_KEYDOWN)
            {
                auto key = event.key.keysym.sym;
                if (key == SDLK_w)
                    wireframe = !wireframe;
                else if (key == SDLK_ESCAPE) break;
            }
        }

        float now = SDL_GetTicks() / 1000.f;
        dt = now - previous_time;
        previous_time = now;

        rotation += rotation_speed * dt;

        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        glUseProgram(shader.id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture.id);
        glUniform1i(texture_loc, 0);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x_position, -0.7f, z_position));
        model = glm::scale(model, glm::vec3(0.02f, 0.02f, 0.02f));
        model = glm::rotate(model, (float)3.14159/5, glm::vec3(1.f, 0.f, 0.f));
        model = glm::rotate(model, rotation, glm::vec3(0.f, 1.f, 0.f));
        setModelTransform(shader, model);
        setCameraTransform(shader, glm::lookAt(glm::vec3(0.f, 0.f, 2.f), glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 1.f, 0.f)));
        setProjectionTransform(shader, glm::perspective(45.f, (float)screen_width / screen_height, 0.1f, 100.f));
        draw(mesh);

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
