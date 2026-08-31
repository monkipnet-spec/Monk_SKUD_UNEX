#include "skud/App.h"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
static skud::App* g_app=nullptr;
static void sig(int){if(g_app)g_app->stop();}
int main(int argc,char**argv){std::string root=argc>1?argv[1]:std::filesystem::current_path().string();skud::App app(root);g_app=&app;std::signal(SIGINT,sig);std::signal(SIGTERM,sig);if(!app.init())return 1;return app.run()?0:2;}
