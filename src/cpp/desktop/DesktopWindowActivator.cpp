/*
 * DesktopWindowActivator.cpp
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * This program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include "DesktopWindowActivator.hpp"


#ifdef Q_OS_LINUX

#include <dlfcn.h>

#include <QGtkStyle>
#include <QApplication>
#include <core/SafeConvert.hpp>
#include <core/system/Process.hpp>
#include <core/system/Environment.hpp>
#include <core/system/LibraryLoader.hpp>

#endif


using namespace core;

namespace desktop {

namespace {

#ifdef Q_WS_X11

class GtkLibrary : boost::noncopyable
{
public:
   GtkLibrary()
      : gdk_display_get_default(NULL),
        gdk_x11_window_lookup_for_display(NULL),
        gdk_window_get_user_data(NULL),
        gtk_window_present_with_time(NULL),
        loaded_(false)
   {
      // reset error state
      ::dlerror();

      gdk_display_get_default = (pfn_gdk_display_get_default)
                     ::dlsym(RTLD_DEFAULT, "gdk_display_get_default");
      if (checkForError())
         return;

      gdk_x11_window_lookup_for_display = (pfn_gdk_x11_window_lookup_for_display)
                     ::dlsym(RTLD_DEFAULT, "gdk_x11_window_lookup_for_display");
      if (checkForError())
         return;

      gdk_window_get_user_data = (pfn_gdk_window_get_user_data)
                     ::dlsym(RTLD_DEFAULT, "gdk_window_get_user_data");
      if (checkForError())
         return;

      gtk_window_present_with_time = (pfn_gtk_window_present_with_time)
                     ::dlsym(RTLD_DEFAULT, "gtk_window_present_with_time");
      if (checkForError())
         return;

      loaded_ = true;
   }

   bool loaded() const { return loaded_; }
   std::string error() const { return error_; }

   typedef void*(*pfn_gdk_display_get_default)();
   pfn_gdk_display_get_default gdk_display_get_default;

   typedef void*(*pfn_gdk_x11_window_lookup_for_display)(void*,WId);
   pfn_gdk_x11_window_lookup_for_display gdk_x11_window_lookup_for_display;

   typedef void (*pfn_gdk_window_get_user_data)(void*, void**);
   pfn_gdk_window_get_user_data gdk_window_get_user_data;

   typedef void (*pfn_gtk_window_present_with_time)(void*, int32_t);
   pfn_gtk_window_present_with_time gtk_window_present_with_time;


private:
   bool checkForError()
   {
      const char* error = ::dlerror();
      if (error != NULL)
      {
         error_ = std::string(error);
         return true;
      }
      else
      {
         return false;
      }
   }

private:
   bool loaded_;
   std::string error_;
};

boost::scoped_ptr<GtkLibrary> s_pGtkLibrary;
GtkLibrary& gtkLibrary()
{
   if (!s_pGtkLibrary)
   {
      s_pGtkLibrary.reset(new GtkLibrary());

      if (!s_pGtkLibrary->loaded())
         LOG_ERROR_MESSAGE(s_pGtkLibrary->error());
   }

   return *s_pGtkLibrary;
}

bool isUsingGtk()
{
   // check for gnome session
   system::ProcessResult result;
   Error error = system::runCommand("pidof gnome-session",
                                    system::ProcessOptions(),
                                    &result);
   if (error)
   {
      LOG_ERROR(error);
      return false;
   }
   if ((result.exitStatus != EXIT_SUCCESS) || result.stdOut.empty())
      return false;

   // extra paranoid check for KDE environment variable
   if (!system::getenv("KDE_FULL_SESSION").empty())
      return false;

   // check for GtkStyle
   return dynamic_cast<QGtkStyle*>(QApplication::style()) != NULL;
}

void forceActivateGtkWindow(WId xid, int32_t timestamp)
{
   if (isUsingGtk())
   {
      // get gtk library
      GtkLibrary& lib = gtkLibrary();
      if (!lib.loaded())
         return;

      // map the xid to a gdk window
      void* pGdKWindow = lib.gdk_x11_window_lookup_for_display(
                                    lib.gdk_display_get_default(), xid);
      if (pGdKWindow == NULL)
         return;

      // map the gdk window to a gtk window
      void* pGtkWindow = NULL;
      lib.gdk_window_get_user_data(pGdKWindow, &pGtkWindow);
      if (pGtkWindow == NULL)
        return;

      // show the window
      lib.gtk_window_present_with_time(pGtkWindow, timestamp);
   }
}

#endif


} // anonymouys namespace

void raiseAndActivateWindow(QWidget* pWindow, bool force, int32_t timestamp)
{
   //
   // on gnome we need to call gtk_window_present_with_time in order to
   // get into the foreground (see this firefox bug for more details:
   // // https://bugzilla.mozilla.org/show_bug.cgi?id=721498). unfortunately
   // it's not clear that you can even map an X id (which Qt gives us)
   // to a GtkWindow id (Qt might not even be using a GtkWindow for its
   // outer frame window)
   //
   if (force)
   {
#ifdef Q_WS_X11
      /* NOTE: we also tried getting the main X window id for the
         process but that didn't work either
      */
      //std::string windowId = system::getenv("WINDOWID");
      //WId xid = safe_convert::stringTo<WId>(windowId, 0);

      forceActivateGtkWindow(pWindow->effectiveWinId(), timestamp);
#endif
   }

   if (pWindow->isMinimized())
   {
      pWindow->setWindowState(
                     pWindow->windowState() & ~Qt::WindowMinimized);
   }

   pWindow->raise();
   pWindow->activateWindow();
}

} // namespace desktop
