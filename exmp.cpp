#include <iostream>
#include <vector>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <CL/cl.h>
#include <SFML/Graphics.hpp>

const int width = 1280;
const int height = 720;
#define SIZE (width * height)

//  OpenCL kernel — double precision + smooth colouring + Mandelbrot / Lemniscate

const char* KernelSource = R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable

__kernel void fractal(
    __global float* output,
    int    width,
    int    height,
    double zoom,
    double offsetX,
    double offsetY,
    int    maxIter,
    int    lemniscateMode,
    double lemniscateRe,
    double lemniscateIm)
{
    int id = get_global_id(0);
    int px = id % width;
    int py = id / width;

    double cx, cy, zx, zy;

    if (lemniscateMode == 0)
    {
        cx = (px - width  * 0.5) / zoom + offsetX;
        cy = (py - height * 0.5) / zoom + offsetY;
        zx = 0.0;
        zy = 0.0;
    }
    else
    {
        zx = (px - width  * 0.5) / zoom + offsetX;
        zy = (py - height * 0.5) / zoom + offsetY;
        cx = lemniscateRe;
        cy = lemniscateIm;
    }

    int i = 0;
    while (zx*zx + zy*zy < 256.0 && i < maxIter)
    {
        double tmp = zx*zx - zy*zy + cx;
        zy = 2.0 * zx * zy + cy;
        zx = tmp;
        i++;
    }

    // Smooth escape time — eliminates colour banding
    float smooth = (float)i;
    if (i < maxIter)
    {
        double log2z = log(zx*zx + zy*zy) * 0.5;
        double nu    = log(log2z / log(2.0)) / log(2.0);
        smooth = (float)((double)i + 1.0 - nu);
    }

    output[id] = smooth;
}
)CLC";

//  Animated palette  (hue rotates with time)
sf::Color palette(float t, float time, int maxIter)
{
    if (t >= (float)maxIter) return sf::Color::Black;   // inside set

    // Normalise + shift by time → colours "flow"
    float phase = t * 0.05f + time * 0.3f;

    // Three-wave RGB with different frequencies for rich hues
    float r = 0.5f + 0.5f * sin(phase * 1.0f);
    float g = 0.5f + 0.5f * sin(phase * 1.3f + 2.094f);   // 2π/3
    float b = 0.5f + 0.5f * sin(phase * 0.7f + 4.189f);   // 4π/3

    // Brightness boost near boundary
    float bright = 1.0f - exp(-t * 0.08f);
    r *= bright;  g *= bright;  b *= bright;

    return sf::Color(
        static_cast<std::uint8_t>(r * 255),
        static_cast<std::uint8_t>(g * 255),
        static_cast<std::uint8_t>(b * 255)
    );
}

//  Simple HUD text line
void drawHUD(sf::RenderWindow& window, sf::Font& font,
    double zoom, double ox, double oy,
    int maxIter, float fps, bool lemniscate,
    double lemniscateRe, double lemniscateIm, bool autoZoom)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6)
        << (lemniscate ? "[LEMNISCATE]" : "[MANDELBROT]")
        << "  zoom=" << std::setprecision(1) << zoom
        << "  x=" << std::setprecision(6) << ox
        << "  y=" << oy
        << "  iter=" << maxIter
        << "  fps=" << std::setprecision(1) << fps;

    if (lemniscate)
        ss << "  c=(" << std::setprecision(3) << lemniscateRe << "," << lemniscateIm << ")";

    if (autoZoom) ss << "  [AUTO-ZOOM]";

    sf::Text hud(font, ss.str(), 14);
    hud.setFillColor(sf::Color(220, 220, 220, 200));
    hud.setPosition({ 8.f, 4.f });

    // Shadow
    sf::Text shadow = hud;
    shadow.setFillColor(sf::Color(0, 0, 0, 160));
    shadow.setPosition({ 9.f, 5.f });

    window.draw(shadow);
    window.draw(hud);

    // Controls hint at bottom
    std::string hint =
        "Scroll=zoom  Drag=pan  J=lemniscate  A=auto-zoom  R=reset  +/-=iterations  P=palette speed";
    sf::Text hint_txt(font, hint, 12);
    hint_txt.setFillColor(sf::Color(180, 180, 180, 160));
    hint_txt.setPosition({ 8.f, (float)window.getSize().y - 20.f });
    window.draw(hint_txt);
}

int main()
{
    // OpenCL setup
    cl_int err;
    cl_platform_id platform;
    cl_device_id   device;

    clGetPlatformIDs(1, &platform, nullptr);
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);

    cl_context context =
        clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);

    cl_command_queue queue =
        clCreateCommandQueueWithProperties(context, device, 0, &err);

    cl_program program =
        clCreateProgramWithSource(context, 1, &KernelSource, nullptr, &err);

    if (clBuildProgram(program, 0, nullptr, nullptr, nullptr, nullptr) != CL_SUCCESS)
    {
        char log[4096];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
            sizeof(log), log, nullptr);
        std::cerr << "Build error:\n" << log << "\n";
        return 1;
    }

    cl_kernel kernel = clCreateKernel(program, "fractal", &err);

    cl_mem outputBuf = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
        sizeof(float) * SIZE, nullptr, nullptr);

    std::vector<float> result(SIZE);

    //SFML window 
    sf::RenderWindow window(sf::VideoMode({ (unsigned)width, (unsigned)height }),
        "Fractal Explorer Made By Cherine & Yousra.",
        sf::Style::Default);
    window.setFramerateLimit(60);

    // Use a monospace font bundled with most Linux/Windows installs,
    // or fall back gracefully.
    sf::Font font;
    bool fontLoaded = font.openFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf") ||
        font.openFromFile("C:/Windows/Fonts/consola.ttf") ||
        font.openFromFile("/System/Library/Fonts/Menlo.ttc");

    sf::VertexArray points(sf::PrimitiveType::Points, SIZE);

    // Fractal state 
    double zoom = 300.0;
    double offsetX = -0.5;
    double offsetY = 0.0;
    int    maxIter = 300;

    // Lemniscate
    bool   lemniscateMode = false;
    double lemniscateRe = -0.7269;
    double lemniscateIm = 0.1889;

    // Auto-zoom target — a classic spiral
    const double TARGET_X = -0.10109636384562;
    const double TARGET_Y = 0.95628651080914;
    bool  autoZoom = false;

    // Palette animation
    float paletteTime = 0.0f;
    float paletteSpeed = 1.0f;

    // Drag
    bool  dragging = false;
    sf::Vector2i lastMouse;

    // FPS
    sf::Clock clock;
    float fps = 60.f;

    // Main loop 
    while (window.isOpen())
    {
        float dt = clock.restart().asSeconds();
        fps = fps * 0.9f + (1.f / dt) * 0.1f;   // smoothed
        paletteTime += dt * paletteSpeed;

        // Events
        while (auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();

            // Zoom with scroll wheel
            if (auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>())
            {
                double factor = (wheel->delta > 0) ? 1.15 : 1.0 / 1.15;
                double mx = (double)wheel->position.x;
                double my = (double)wheel->position.y;
                double wx = (mx - width * 0.5) / zoom + offsetX;
                double wy = (my - height * 0.5) / zoom + offsetY;
                zoom *= factor;
                offsetX = wx - (mx - width * 0.5) / zoom;
                offsetY = wy - (my - height * 0.5) / zoom;
                autoZoom = false;
            }

            // Drag
            if (auto* p = event->getIf<sf::Event::MouseButtonPressed>())
                if (p->button == sf::Mouse::Button::Left)
                {
                    dragging = true; lastMouse = sf::Mouse::getPosition(window);
                }

            if (auto* r = event->getIf<sf::Event::MouseButtonReleased>())
                if (r->button == sf::Mouse::Button::Left)
                    dragging = false;

            // Keyboard
            if (auto* k = event->getIf<sf::Event::KeyPressed>())
            {
                switch (k->code)
                {
                case sf::Keyboard::Key::R:   // Reset
                    zoom = 300.0; offsetX = -0.5; offsetY = 0.0;
                    maxIter = 300; autoZoom = false; lemniscateMode = false;
                    break;

                case sf::Keyboard::Key::J:   // Toggle Lemniscate / Mandelbrot
                    lemniscateMode = !lemniscateMode;
                    if (lemniscateMode) { zoom = 300.0; offsetX = 0.0; offsetY = 0.0; }
                    else { zoom = 300.0; offsetX = -0.5; offsetY = 0.0; }
                    break;

                case sf::Keyboard::Key::A:   // Auto-zoom toggle
                    autoZoom = !autoZoom;
                    if (autoZoom && !lemniscateMode)
                    {
                        offsetX = TARGET_X; offsetY = TARGET_Y;
                    }
                    break;

                case sf::Keyboard::Key::Equal:   // + more iterations
                case sf::Keyboard::Key::Add:
                    maxIter = std::min(maxIter + 100, 3000);
                    break;

                case sf::Keyboard::Key::Hyphen:  // - fewer iterations
                case sf::Keyboard::Key::Subtract:
                    maxIter = std::max(maxIter - 100, 50);
                    break;

                case sf::Keyboard::Key::P:   // faster palette cycle
                    paletteSpeed = (paletteSpeed < 3.0f) ? paletteSpeed + 0.5f : 0.0f;
                    break;

                    // Animate Lemniscate constant with arrow keys
                case sf::Keyboard::Key::Left:  lemniscateRe -= 0.005; break;
                case sf::Keyboard::Key::Right: lemniscateRe += 0.005; break;
                case sf::Keyboard::Key::Up:    lemniscateIm -= 0.005; break;
                case sf::Keyboard::Key::Down:  lemniscateIm += 0.005; break;

                default: break;
                }
            }
        }

        // Pan 
        if (dragging)
        {
            sf::Vector2i mouse = sf::Mouse::getPosition(window);
            sf::Vector2i delta = mouse - lastMouse;
            offsetX -= (double)delta.x / zoom;
            offsetY -= (double)delta.y / zoom;
            lastMouse = mouse;
            autoZoom = false;
        }

        // Auto-zoom 
        if (autoZoom)
        {
            zoom *= 1.008;
            maxIter = std::min((int)(300 + log(zoom / 300.0) * 80), 3000);
        }

        // Run OpenCL kernel
        int lemniscateInt = lemniscateMode ? 1 : 0;
        clSetKernelArg(kernel, 0, sizeof(cl_mem), &outputBuf);
        clSetKernelArg(kernel, 1, sizeof(int), &width);
        clSetKernelArg(kernel, 2, sizeof(int), &height);
        clSetKernelArg(kernel, 3, sizeof(cl_double), &zoom);
        clSetKernelArg(kernel, 4, sizeof(cl_double), &offsetX);
        clSetKernelArg(kernel, 5, sizeof(cl_double), &offsetY);
        clSetKernelArg(kernel, 6, sizeof(int), &maxIter);
        clSetKernelArg(kernel, 7, sizeof(int), &lemniscateInt);
        clSetKernelArg(kernel, 8, sizeof(cl_double), &lemniscateRe);
        clSetKernelArg(kernel, 9, sizeof(cl_double), &lemniscateIm);

        size_t global = SIZE;
        clEnqueueNDRangeKernel(queue, kernel, 1, nullptr,
            &global, nullptr, 0, nullptr, nullptr);
        clFinish(queue);

        clEnqueueReadBuffer(queue, outputBuf, CL_TRUE, 0,
            sizeof(float) * SIZE,
            result.data(), 0, nullptr, nullptr);

        // Colour pixels 
        for (int i = 0; i < SIZE; i++)
        {
            points[i].position = sf::Vector2f(
                (float)(i % width),
                (float)(i / width));
            points[i].color = palette(result[i], paletteTime, maxIter);
        }

        // Draw 
        window.clear();
        window.draw(points);

        if (fontLoaded)
            drawHUD(window, font, zoom, offsetX, offsetY,
                maxIter, fps, lemniscateMode, lemniscateRe, lemniscateIm, autoZoom);

        window.display();
    }

    // Cleanup
    clReleaseMemObject(outputBuf);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);

    return 0;
}