#include "keys.h"
#include "fsx.h"

namespace Keys {

std::vector<String> list() {
  std::vector<String> out;
  File dir = fsx::fs().open(fsx::keysDir());
  if (!dir || !dir.isDirectory()) return out;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    String n = f.name();
    int slash = n.lastIndexOf('/');     // some cores return full paths
    if (slash >= 0) n = n.substring(slash + 1);
    out.push_back(n);
  }
  return out;
}

String read(const String& name) {
  if (name.isEmpty()) return "";
  File f = fsx::fs().open(fsx::keysDir() + "/" + name, "r");
  if (!f) return "";
  String pem = f.readString();
  f.close();
  return pem;
}

}  // namespace Keys
