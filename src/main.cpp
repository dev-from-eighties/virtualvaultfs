#include "app/App.hpp"
#include <iostream>

int main(int argc, char** argv)
{
    virtualvaultfs::app::App app;
    return app.run(argc, argv);
}
