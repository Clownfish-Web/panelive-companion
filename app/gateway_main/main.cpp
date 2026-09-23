#include "platform_companion_application.hpp"

int main(int argc, char** argv)
{
  auto application = studio_remote::create_platform_companion_application();
  return application->run(argc, argv);
}
