/**
 * Copyright 2019 Lucas Walter
 */

#include <ddynamic_reconfigure/ddynamic_reconfigure.h>
#include <experimental/filesystem>
#include <dlfcn.h>
#include <frei0r.h>
#include <frei0r_image/LoadPlugin.h>
#include <frei0r_image/frei0r_image.hpp>
#include <map>
#include <ros/ros.h>
#include <string>
#include <vector>

namespace frei0r_image
{

bool getPluginInfo(const std::string& name, std::string& plugin_name, int& plugin_type)
{
  void* handle = dlopen(name.c_str(), RTLD_NOW);
  if (!handle) {
    return false;
  }
  f0r_get_plugin_info_t get_plugin_info = (f0r_get_plugin_info_t)dlsym(handle, "f0r_get_plugin_info");
  if (!get_plugin_info) {
    dlclose(handle);
    return false;
  }
  f0r_plugin_info_t info;
  get_plugin_info(&info);
  plugin_name = info.name;
  plugin_type = info.plugin_type;
  dlclose(handle);
  return true;
}

struct Selector
{
  std::unique_ptr<ddynamic_reconfigure::DDynamicReconfigure> ddr_;
  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::ServiceClient load_client_;

  Selector() :
    private_nh_("~")
  {
    // TODO(lucasw) one of the ddr variables could be a path name
    std::vector<std::string> plugin_dirs = {
      // "/usr/lib/frei0r-1/",  // ubuntu frei0r-plugins puts them here
      // "/usr/local/lib/frei0r-1/",
      //  "/.frei0r-1/lib"  // TODO(lucasw) need to prefix $HOME to this
    };
    std::string custom_path = "/usr/lib/frei0r-1/";
    private_nh_.getParam("path", custom_path);
    ROS_INFO_STREAM("custom search path: " << custom_path);
    plugin_dirs.push_back(custom_path);

    load_client_ = nh_.serviceClient<LoadPlugin>("load_plugin");

    ddr_ = std::make_unique<ddynamic_reconfigure::DDynamicReconfigure>(private_nh_);

    std::map<std::string, std::string> enum_map;
    enum_map["none"] = "none";

    // std::vector<std::string> plugin_names;
    for (const auto& dir : plugin_dirs) {
      if (!std::experimental::filesystem::exists(dir)) {
        continue;
      }
      try {
        for (const auto& entry : std::experimental::filesystem::directory_iterator(dir)) {
          // TODO(lucasw) get the name and type of the plugin
          // and use it here.
          const auto path = entry.path();
          std::string name;
          int plugin_type = 0;
          // ROS_INFO_STREAM(path);
          const bool rv = getPluginInfo(path, name, plugin_type);
          if (!rv) {
            continue;
          }
          if (!((plugin_type == F0R_PLUGIN_TYPE_SOURCE) ||
                (plugin_type == F0R_PLUGIN_TYPE_FILTER))) {
            continue;
          }

          ROS_INFO_STREAM(plugin_type << " " << name);
          // const std::string name = info.name;
          enum_map[name] = path;
        }
      } catch (std::experimental::filesystem::v1::__cxx11::filesystem_error& ex) {
        std::cout << dir << " " << ex.what() << "\n";
      }
    }
    ddr_->registerEnumVariable<std::string>("frei0r", "none",
        boost::bind(&Selector::select, this, _1), "frei0r", enum_map);

#if 0
    std::map<std::string, bool> bad_frei0rs;
    bad_frei0rs["/usr/lib/frei0r-1/curves.so"] = true;

    for (const auto& plugin_name : plugin_names) {
      if (bad_frei0rs.count(plugin_name) > 0) {
        std::cout << "skipping " << plugin_name << "\n";
        continue;
      }
      auto plugin = std::make_shared<frei0r_image::Frei0rImage>();
      plugin->loadLibrary(plugin_name);
      const int plugin_type = plugin->fi_.plugin_type;
      plugins[plugin_type][plugin_name] = plugin;
      std::cout << plugin_type << " " << plugin_name << "\n";
    }

    for (const auto& plugin_pair : plugins[1]) {
      plugin_pair.second->print();
    }
#endif

    ddr_->publishServicesTopics();
  }

  void select(const std::string plugin_path)
  {
    LoadPlugin srv;
    srv.request.plugin_path = plugin_path;
    if (!load_client_.waitForExistence(ros::Duration(0.1))) {
      ROS_ERROR_STREAM("no service yet");
      return;
    }
    load_client_.call(srv);
  }
};

}  // namespace frei0r_image

int main(int argn, char* argv[])
{
  ros::init(argn, argv, "select_plugin");
  auto selector = frei0r_image::Selector();
  ros::spin();

  return 0;
}
