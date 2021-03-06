#ifndef XML_INTERFACE_H
#define XML_INTERFACE_H

#include <sstream> // for stringstream
#include <string>
#include <vector>

#include "pugixml.hpp"


namespace openmc {

inline bool
check_for_node(pugi::xml_node node, const char *name)
{
  return node.attribute(name) || node.child(name);
}

std::string get_node_value(pugi::xml_node node, const char *name,
  bool lowercase=false, bool strip=false);

template <typename T>
std::vector<T> get_node_array(pugi::xml_node node, const char* name)
{
  // Get value of node attribute/child
  std::string s {get_node_value(node, name)};

  // Read values one by one into vector
  std::stringstream iss {s};
  T value;
  std::vector<T> values;
  while (iss >> value)
    values.push_back(value);

  return values;
}

} // namespace openmc
#endif // XML_INTERFACE_H
