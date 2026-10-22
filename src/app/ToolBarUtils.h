#ifndef TOOLBARUTILS_H
#define TOOLBARUTILS_H

#include <wx/aui/auibar.h>
#include <wx/bmpbndl.h>
#include <wx/toolbar.h>

// Adding tools with a disabled image that fits the theme of the application.
// The default disabled image of a tool is shiny and white, which looks wrong on
// a dark theme.
namespace toolbar {

// The disabled version of `bundle`: the same sizes, with the colors faded to
// the background of the theme (dark or light). It follows the scale of the
// display.
wxBitmapBundle MakeDisabledBundle(const wxBitmapBundle &bundle);

// The style of a toolbar that fits the theme of the application: a plain
// background on a light theme, the default one on a dark theme. Call it when
// the toolbar is created, and again when the theme changes (the toolbar is
// laid out and drawn again).
void ApplyStyle(wxAuiToolBar *toolbar);

// Adds a tool with a disabled image made by MakeDisabledBundle(). The help
// string is the label when it is empty. A drop-down tool (wxITEM_DROPDOWN) is a
// normal tool with the drop-down arrow.
wxAuiToolBarItem *AddTool(wxAuiToolBar *toolbar, int toolId,
                          const wxString &label, const wxBitmapBundle &bitmap,
                          const wxString &shortHelp = wxEmptyString,
                          wxItemKind kind = wxITEM_NORMAL);

// The same for the native toolbar (GTK and macOS).
wxToolBarToolBase *AddTool(wxToolBarBase *toolbar, int toolId,
                           const wxString &label, const wxBitmapBundle &bitmap,
                           const wxString &shortHelp = wxEmptyString,
                           wxItemKind kind = wxITEM_NORMAL);

} // namespace toolbar

#endif // TOOLBARUTILS_H
