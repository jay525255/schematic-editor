#include "MainFrame.h"
#include "Ids.h"
#include "SchematicIO.h"
#include "KiCadExport.h"
#include <set>
#include <map>
#include <wx/uiaction.h>
#include <wx/propgrid/propgrid.h>
#include <wx/dcscreen.h>
#include <wx/dcmemory.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/timer.h>
#include <wx/stopwatch.h>
#include <wx/utils.h>
#include <wx/filefn.h>
#include <cstring>
#include <iostream>
#include <functional>
#include <stdexcept>
class NativeApp:public wxApp {public:bool OnInit()override{wxInitAllImageHandlers();return true;}};
wxIMPLEMENT_APP_NO_MAIN(NativeApp);
namespace {
se::MainFrame* frame=nullptr;wxString out;wxUIActionSimulator* input=nullptr;
void pump(int ms=150){wxStopWatch clock;while(clock.Time()<ms){wxYield();wxTheApp->ProcessPendingEvents();wxMilliSleep(10);}}
bool screenshot(const wxString& name){
#ifdef __WXGTK__
    if(!frame)return false;
    frame->Update();pump(100);
    // A fresh X11 capture avoids wxScreenDC returning its first cached image.
    const auto path=(out+"/"+name).utf8_string();
    const char* const args[]={"scrot","--overwrite","--focused",path.c_str(),nullptr};
    return wxExecute(args,wxEXEC_SYNC|wxEXEC_NOEVENTS)==0&&wxFileExists(wxString::FromUTF8(path.c_str()));
#else
    if(!frame)return false;auto r=frame->GetScreenRect();wxScreenDC screen;wxBitmap bitmap(r.width,r.height);
    wxMemoryDC memory(bitmap);bool ok=memory.Blit(0,0,r.width,r.height,&screen,r.x,r.y);memory.SelectObject(wxNullBitmap);
    return ok&&bitmap.ConvertToImage().SaveFile(out+"/"+name,wxBITMAP_TYPE_PNG);
#endif
}
bool differentScreenshots(const wxString& first,const wxString& second){
    wxImage a(out+"/"+first,wxBITMAP_TYPE_PNG),b(out+"/"+second,wxBITMAP_TYPE_PNG);
    if(!a.IsOk()||!b.IsOk())return false;
    if(a.GetWidth()!=b.GetWidth()||a.GetHeight()!=b.GetHeight())return true;
    return std::memcmp(a.GetData(),b.GetData(),static_cast<size_t>(a.GetWidth())*a.GetHeight()*3)!=0;
}
void check(bool ok,const char* message){if(!ok){screenshot("failure.png");throw std::runtime_error(message);}std::cout<<"PASS "<<message<<std::endl;}
void key(int code,int modifiers=wxMOD_NONE){check(input->Char(code,modifiers),"native keyboard delivered");pump();}
void click(wxWindow* window,const wxPoint& pos){check(input->MouseMove(window->ClientToScreen(pos))&&input->MouseClick(),"native mouse delivered");pump();}
wxPropertyGrid* propertyGrid(wxWindow* window) {
    if(auto* grid=dynamic_cast<wxPropertyGrid*>(window))return grid;
    for(auto* child:window->GetChildren())if(auto* grid=propertyGrid(child))return grid;
    return nullptr;
}
using Partition=std::set<std::set<wxString>>;
Partition partition(const se::SchematicCanvas& canvas) {
    const auto& cs=canvas.GetComponents(); const auto& ws=canvas.GetWires();
    auto graph=se::NetBuilder::Build(cs,ws);Partition result;
    for(const auto& net:graph.nets){std::set<wxString> nodes;for(const auto& p:net.pins)nodes.insert(cs[p.compIndex].refdes+"."+se::PcbPinNumber(cs[p.compIndex],p.pinIndex));result.insert(nodes);}return result;
}
struct Later:wxTimer{std::function<void()> f;explicit Later(std::function<void()> fn):f(std::move(fn)){}void Notify()override{f();}};
void fileCommand(int modifiers,int code,const wxString& filename){
    bool typed=false;
    Later fill([&]{typed=input->Char('L',wxMOD_CONTROL)&&input->Char('A',wxMOD_CONTROL)&&input->Text(filename.utf8_str())&&input->Char(WXK_RETURN);});
    Later accept([&]{input->Char(WXK_RETURN);});
    Later watchdog([]{screenshot("dialog-timeout.png");std::cerr<<"FAIL file chooser timed out\n";std::exit(1);});
    fill.StartOnce(750);accept.StartOnce(1600);watchdog.StartOnce(15000);
    check(input->Char(code,modifiers),"native file shortcut delivered");pump(2100);
    fill.Stop();accept.Stop();watchdog.Stop();check(typed,"native file path typed");
}
}
int main(int argc,char**argv){
    if(argc!=4){std::cerr<<"expected output directory\n";return 2;}
    out=wxFileName(wxString::FromUTF8(argv[1]),"").GetFullPath();wxFileName::Mkdir(out,wxS_DIR_DEFAULT,wxPATH_MKDIR_FULL);
    int wxargc=1;if(!wxEntryStart(wxargc,argv)){std::cerr<<"FAIL no display, no GUI operations executed\n";return 1;}
    if(!wxTheApp->CallOnInit())return 1;wxLog::SetActiveTarget(new wxLogStderr());
    try{
        se::ComponentCatalog::SetLibraryPath(wxFileName(out,"components.xml").GetFullPath());
        wxUIActionSimulator native;input=&native;frame=new se::MainFrame;frame->Show();frame->Raise();pump(700);
        check(!frame->GetMenuBar()->IsEnabled(se::ID_PLACE_BUS)&&!frame->GetMenuBar()->IsEnabled(se::ID_TOOLS_GENERATE_BOM)&&frame->GetMenuBar()->IsEnabled(se::ID_PLACE_WIRE),"planned menu commands are disabled and working commands stay enabled");
        auto* canvas=frame->Canvas();auto* tree=dynamic_cast<wxTreeCtrl*>(frame->FindWindow(se::ID_LIBRARY_TREE));check(tree,"library tree is real and accessible");
        wxTreeItemIdValue catCookie,leafCookie;auto cat=tree->GetFirstChild(tree->GetRootItem(),catCookie);auto leaf=tree->GetFirstChild(cat,leafCookie);
        auto selectGate=[&]{wxRect r;tree->EnsureVisible(leaf);pump();check(tree->GetBoundingRect(leaf,r,true),"gate row visible");click(tree,r.GetPosition()+wxPoint(10,r.height/2));check(canvas->GetPendingComponent()!=nullptr,"native selection activates placement");};
        selectGate();click(canvas,canvas->ScreenPoint({-120,-80}));selectGate();click(canvas,canvas->ScreenPoint({100,80}));check(canvas->GetComponentCount()==2,"mouse places two gate instances");
        check(canvas->GetComponents()[0].refdes!=canvas->GetComponents()[1].refdes,"placed component references are unique");
        auto original=canvas->GetComponents()[0].pos;auto from=se::PinPosition(canvas->GetComponents()[0],2),to=se::PinPosition(canvas->GetComponents()[1],0);
        key('W');check(canvas->GetTool()==se::SchematicCanvas::ToolWire,"W activates wiring");click(canvas,canvas->ScreenPoint(from));click(canvas,canvas->ScreenPoint(to));
        auto connected=[&]{const auto& g=canvas->GetNetGraph();return g.pinNet[0][2]==g.pinNet[1][0];};check(canvas->GetWireCount()>0&&connected(),"native wire connects gate terminals");
        const auto start=canvas->ScreenPoint(original+wxPoint(30,20));
        check(input->MouseMove(canvas->ClientToScreen(start))&&input->MouseDown(),"native drag begins");pump();
        check(input->MouseMove(canvas->ClientToScreen(start+wxPoint(20,20))),"native drag moves");pump();check(input->MouseUp(),"native drag ends");pump();
        check(canvas->GetComponents()[0].pos!=original&&connected(),"drag retains intended connection");
        key('Z',wxMOD_CONTROL);check(canvas->GetComponents()[0].pos==original&&connected(),"undo restores geometry and connection");
        key('Y',wxMOD_CONTROL);check(canvas->GetComponents()[0].pos!=original&&connected(),"redo restores moved geometry");key('Z',wxMOD_CONTROL);
        click(canvas,canvas->ScreenPoint(original+wxPoint(30,20)));
        auto oldTransform=canvas->GetComponents()[0].xform;
        key('R',wxMOD_CONTROL);check(canvas->GetComponents()[0].xform!=oldTransform&&connected(),"native rotation preserves attached wire network");
        key('Z',wxMOD_CONTROL);check(canvas->GetComponents()[0].xform==oldTransform&&connected(),"undo restores rotation and wiring together");
        // Undo deliberately clears selection; select again with a real click.
        click(canvas,canvas->ScreenPoint(original+wxPoint(30,20)));
        key('H',wxMOD_CONTROL);check(canvas->GetComponents()[0].xform!=oldTransform&&connected(),"native flip preserves attached wire network");key('Z',wxMOD_CONTROL);
        check(screenshot("editing.png"),"save actual editing screenshot");
        key('A',wxMOD_CONTROL);key('C',wxMOD_CONTROL);key('V',wxMOD_CONTROL);check(canvas->GetComponentCount()==4,"native copy paste adds independent instances");key('Z',wxMOD_CONTROL);
        auto saved=wxFileName(out,"native-roundtrip.ses").GetFullPath();fileCommand(wxMOD_CONTROL|wxMOD_SHIFT,'S',saved);
        check(wxFileExists(saved)&&!canvas->IsModified(),"native save-as writes design");key('N',wxMOD_CONTROL);check(canvas->GetComponentCount()==0,"native new clears saved design");
        fileCommand(wxMOD_CONTROL,'O',saved);check(canvas->GetComponentCount()==2&&connected(),"native open restores saved circuit");
        canvas->StartSimulation();
        key('A',wxMOD_CONTROL);key(WXK_DELETE);check(canvas->GetComponentCount()==0,"native Delete removes selected objects");check(!canvas->IsSimulating(),"topology edit invalidates runtime state");key('Z',wxMOD_CONTROL);check(canvas->GetComponentCount()==2,"native undo restores deletion");
        check(screenshot("roundtrip.png"),"save real roundtrip screenshot");
        key('S',wxMOD_CONTROL);
        fileCommand(wxMOD_CONTROL,'O',wxString::FromUTF8(argv[2]));
        check(canvas->GetComponentCount()==3,"native open loads dynamic AND circuit");
        key(WXK_F5);pump(450);check(canvas->IsSimulating()&&canvas->GetSimulator().RuntimeTime()>=3,"native Run advances real timer");
        key(WXK_F5);check(!canvas->IsSimulating(),"native Pause stops automatic timer");
        auto paused=canvas->GetSimulator().RuntimeTime();pump(350);
        check(canvas->GetSimulator().RuntimeTime()==paused,"pause preserves exact simulation time");
        auto outputNet=canvas->GetNetGraph().pinNet[2][2];
        check(canvas->GetSimulator().NetValue(outputNet)==se::LogicValue::L,"AND starts with low output");
        click(canvas,canvas->ScreenPoint({-180,-60}));
        check(canvas->GetSimulator().InputValue(0)==se::LogicValue::H,"native mouse changes first digital input");
        key(WXK_F6);check(canvas->GetSimulator().RuntimeTime()==paused+1,"native step advances exactly one tick");
        click(canvas,canvas->ScreenPoint({-180,60}));
        check(canvas->GetSimulator().InputValue(1)==se::LogicValue::H,"native mouse changes second digital input");
        check(canvas->GetSimulator().NetValue(outputNet)==se::LogicValue::L,"output waits for propagation event");
        key(WXK_F6);check(canvas->GetSimulator().NetValue(outputNet)==se::LogicValue::H,"native step propagates high through AND");
        check(screenshot("dynamic-simulation.png"),"save actual dynamic signal screenshot");
        check(differentScreenshots("editing.png","dynamic-simulation.png"),"different circuit stages have fresh distinct screenshots");
        key(WXK_F5);pump(250);key(WXK_F5);check(canvas->GetSimulator().RuntimeTime()>paused+2,"native Resume continues existing timeline");
        key(WXK_F5,wxMOD_SHIFT);check(!canvas->GetSimulator().RuntimeReady(),"native Reset clears pending events");
        canvas->MarkSaved();
        fileCommand(wxMOD_CONTROL|wxMOD_SHIFT,'I',wxString::FromUTF8(argv[3]));
        check(canvas->GetComponentCount()==9&&canvas->GetWireCount()==15&&canvas->IsModified(),"native KiCad import menu shortcut loads real rectifier and marks unsaved");
        const auto importedPartition=partition(*canvas);
        check(screenshot("kicad-import.png"),"save actual imported KiCad drawing screenshot");
        size_t resistor=0;while(resistor<canvas->GetComponents().size()&&canvas->GetComponents()[resistor].refdes!="R1")++resistor;
        check(resistor<canvas->GetComponents().size(),"real imported R1 exists");
        auto beforeMove=canvas->GetComponents()[resistor].pos;
        const auto at=canvas->ScreenPoint({11811,9398});click(canvas,at);
        check(canvas->GetSelectedComponentIndex()==static_cast<int>(resistor),"native mouse selects imported resistor");
        // Keep selection and the next drag outside GTK's double-click interval.
        pump(550);
        check(input->MouseMove(canvas->ClientToScreen(at))&&input->MouseDown(),"native imported-symbol drag starts");pump();
        check(input->MouseMove(canvas->ClientToScreen(at+wxPoint(20,20))),"native imported-symbol drag moves");pump();
        check(input->MouseUp(),"native imported-symbol drag ends");pump();
        check(canvas->GetComponents()[resistor].pos!=beforeMove,"native imported resistor changes position");
        check(partition(*canvas)==importedPartition,"native imported resistor movement retains electrical connections");
        key('Z',wxMOD_CONTROL);check(canvas->GetComponents()[resistor].pos==beforeMove&&partition(*canvas)==importedPartition,"undo restores imported symbol and wires");
        key('Y',wxMOD_CONTROL);check(canvas->GetComponents()[resistor].pos!=beforeMove,"redo moves imported resistor");
        // Undo/redo intentionally clears selection, so select the moved instance again.
        click(canvas,canvas->ScreenPoint(wxPoint(11811,9398)+canvas->GetComponents()[resistor].pos-beforeMove));
        check(canvas->GetSelectedComponentIndex()==static_cast<int>(resistor),"native mouse selects moved imported resistor for properties");
        auto* grid=propertyGrid(frame);check(grid,"real property grid found");
        auto* valueProperty=grid->GetPropertyByLabel(L"元件值");check(valueProperty,"imported value property visible");
        grid->EnsureVisible(valueProperty);pump();auto valueRect=grid->GetPropertyRect(valueProperty,valueProperty);
        click(grid,grid->CalcScrolledPosition(wxPoint(grid->GetSplitterPosition()+35,valueRect.y+valueRect.height/2)));
        key('A',wxMOD_CONTROL);check(input->Text("2k"),"native keyboard edits imported resistor value");key(WXK_RETURN);
        check(canvas->GetComponents()[resistor].value=="2k","imported value change reaches real model");
        canvas->SetFocus();pump();
        size_t r2=0;while(r2<canvas->GetComponents().size()&&canvas->GetComponents()[r2].refdes!="R2")++r2;
        check(r2<canvas->GetComponents().size(),"second imported resistor exists");
        auto branchStart=se::PinPosition(canvas->GetComponents()[r2],1);auto branchEnd=branchStart+wxPoint(400,0);
        const int originalWireCount=canvas->GetWireCount();
        key('W');click(canvas,canvas->ScreenPoint(branchStart));click(canvas,canvas->ScreenPoint(branchEnd));key(WXK_ESCAPE);
        check(canvas->GetWireCount()>originalWireCount&&partition(*canvas)==importedPartition,"native wiring adds a branch to an imported electrical pin");
        click(canvas,canvas->ScreenPoint(branchStart+wxPoint(200,0)));key(WXK_DELETE);
        check(canvas->GetWireCount()==originalWireCount&&partition(*canvas)==importedPartition,"native selection and Delete edit an imported circuit wire");
        check(screenshot("kicad-edited.png"),"save edited KiCad screenshot");
        auto importedSave=wxFileName(out,"kicad-native-roundtrip.ses").GetFullPath();fileCommand(wxMOD_CONTROL|wxMOD_SHIFT,'S',importedSave);
        check(wxFileExists(importedSave)&&!canvas->IsModified(),"native save stores imported editable document");
        key('N',wxMOD_CONTROL);check(canvas->GetComponentCount()==0,"new clears imported document after save");
        fileCommand(wxMOD_CONTROL,'O',importedSave);
        check(canvas->GetComponentCount()==9&&partition(*canvas)==importedPartition,"native reopen preserves all imported electrical connections");
        check(canvas->GetComponents()[resistor].value=="2k","native reopen retains edited KiCad parameter");
        check(screenshot("kicad-reopened.png")&&differentScreenshots("kicad-import.png","kicad-edited.png"),"actual imported editing and reopen screenshots captured");
        canvas->MarkSaved();frame->Destroy();pump();frame=nullptr;wxTheApp->OnExit();wxEntryCleanup();return 0;
    }catch(const std::exception& e){screenshot("failure.png");std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
