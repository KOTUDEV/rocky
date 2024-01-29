/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */

/**
* THE DEMO APPLICATION is an ImGui-based app that shows off all the features
* of the Rocky Application API. We intend each "Demo_*" include file to be
* both a unit test for that feature, and a reference or writing your own code.
*/

#include <rocky/vsg/Application.h>
#include <rocky/Version.h>

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QVBoxLayout>

#include <vsgQt/Window.h>

#ifdef ROCKY_HAS_TMS
#include <rocky/TMSImageLayer.h>
#include <rocky/TMSElevationLayer.h>
#endif

ROCKY_ABOUT(qt, qVersion())

using namespace ROCKY_NAMESPACE;

template<class T>
int layerError(T layer)
{
    rocky::Log()->warn("Problem with layer \"" + layer->name() + "\" : " + layer->status().message);
    return -1;
}

//! Specialize the osgQt::Viewer to interoperate with the Rocky Application object
class MyQtViewer : public vsg::Inherit<vsgQt::Viewer, MyQtViewer>
{
public:
    void render() override
    {
        if (continuousUpdate || requests.load() > 0)
        {
            if (frame() == false)
            {
                if (status->cancel())
                {
                    QApplication::quit();
                }
            }
        }
    }

    std::function<bool()> frame;
};


int main(int argc, char** argv)
{
    QApplication qt_app(argc, argv);

    rocky::Application app(argc, argv);

    rocky::Log()->set_level(rocky::log::level::info);

    // Create a customized Qt-based viewer and configure it to render through the App:
    auto viewer = MyQtViewer::create();
    viewer->frame = [&app]() { return app.frame(); };
    viewer->continuousUpdate = true;
    app.setViewer(viewer);

    // Create a Qt window:
    vsg::ref_ptr<vsg::WindowTraits> traits = vsg::WindowTraits::create();
    traits->windowTitle = "Rocky Qt Example";
    traits->width = 1920, traits->height = 1080;
    auto vsg_window = new vsgQt::Window(traits);
    vsg_window->initializeWindow();

    // Add it to our display manager:
    app.displayManager->addWindow(vsg_window->windowAdapter);

    // Not strictly necessary for this demo, but if you are going to open multiple 
    // windows this will allow them to all share the same Vulkan device
    if (!traits->device) 
        traits->device = vsg_window->windowAdapter->getOrCreateDevice();

    // Set up the main Qt window:
    QMainWindow mainWindow;
    mainWindow.setGeometry(traits->x, traits->y, traits->width, traits->height);
    mainWindow.setWindowTitle(traits->windowTitle.c_str());

    // Embed the VSG/rocky window as a widget:
    auto vsg_widget = QWidget::createWindowContainer(vsg_window, &mainWindow);
    mainWindow.setCentralWidget(vsg_widget);

    // Add a simple menu.
    // TODO: The menu functions, but the VSG window overdraws it for some reason.
    auto fileMenu = mainWindow.menuBar()->addMenu("&File");
    fileMenu->addAction("E&xit", &qt_app, &QApplication::quit);

    mainWindow.show();

    // Add some data to the map:
    if (app.mapNode->map->layers().empty())
    {
#ifdef ROCKY_HAS_TMS
        // add an imagery layer to the map
        auto layer = rocky::TMSImageLayer::create();
        layer->uri = "https://readymap.org/readymap/tiles/1.0.0/7";
        app.mapNode->map->layers().add(layer);
        if (layer->status().failed())
            return layerError(layer);

        // add an elevation layer to the map
        auto elev = rocky::TMSElevationLayer::create();
        elev->uri = "https://readymap.org/readymap/tiles/1.0.0/116/";
        app.mapNode->map->layers().add(elev);
        if (elev->status().failed())
            return layerError(elev);
#endif
    }

    // run until the user quits.
    return qt_app.exec();
}
