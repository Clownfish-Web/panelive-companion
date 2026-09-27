#include "linux_management_window.hpp"

#include <exception>

namespace studio_remote::linux
{
void LinuxManagementWindow::update_navigation_width(int width) noexcept
{
  if (width != observed_width_)
  {
    observed_width_ = width;
    queue_layout_redraw();
  }
  if (width <= 750)
    gtk_widget_add_css_class(GTK_WIDGET(window_), "compact-layout");
  else
    gtk_widget_remove_css_class(GTK_WIDGET(window_), "compact-layout");
  const bool narrow = width < 700;
  if (narrow == narrow_window_)
    return;
  narrow_window_ = narrow;
  navigation_expanded_ = narrow ? false : wide_navigation_expanded_;
  apply_navigation_layout();
}

void LinuxManagementWindow::observe_window_size() noexcept
{
  stop_observing_window_size();
  observed_surface_ = gtk_native_get_surface(GTK_NATIVE(window_));
  if (!observed_surface_)
    return;
  surface_layout_handler_ = g_signal_connect(
      observed_surface_,
      "layout",
      G_CALLBACK(+[](GdkSurface*, int width, int, gpointer data) noexcept
                 { static_cast<LinuxManagementWindow*>(data)->update_navigation_width(width); }),
      this);
  update_navigation_width(gdk_surface_get_width(observed_surface_));
}

void LinuxManagementWindow::stop_observing_window_size() noexcept
{
  cancel_layout_redraw();
  observed_width_ = 0;
  if (observed_surface_ && surface_layout_handler_)
    g_signal_handler_disconnect(observed_surface_, surface_layout_handler_);
  surface_layout_handler_ = 0;
  observed_surface_ = nullptr;
}

void LinuxManagementWindow::queue_layout_redraw() noexcept
{
  if (layout_redraw_source_ || !pages_ || !gtk_widget_get_visible(GTK_WIDGET(window_)))
    return;
  // Throttle resize storms without polling the backend or detaching editable controls.
  layout_redraw_source_ = g_timeout_add(
      120,
      +[](gpointer data) noexcept -> gboolean
      {
        auto& owner = *static_cast<LinuxManagementWindow*>(data);
        owner.layout_redraw_source_ = 0;
        try
        {
          if (owner.window_ && owner.pages_ && gtk_widget_get_visible(GTK_WIDGET(owner.window_)))
            owner.redraw();
        }
        catch (const std::exception& error)
        {
          g_warning("Responsive layout redraw failed: %s", error.what());
        }
        catch (...)
        {
          g_warning("Responsive layout redraw failed");
        }
        return G_SOURCE_REMOVE;
      },
      this);
}

void LinuxManagementWindow::cancel_layout_redraw() noexcept
{
  if (layout_redraw_source_)
    g_source_remove(layout_redraw_source_);
  layout_redraw_source_ = 0;
}
} // namespace studio_remote::linux
