#pragma once
#include <filesystem>
#include <memory>
namespace gtracker::sid {
class Editor {
  public:
    Editor(const std::filesystem::path &source, const std::filesystem::path &resources,
           const std::filesystem::path &data);
    ~Editor();
    void draw();
    void request_close();
    bool closed() const;
    bool guitar_requested() const;

  private:
    friend struct EditorTest;
    struct Impl;
    std::unique_ptr<Impl> state;
};
} // namespace gtracker::sid
