#include "TestSupport.h"
#include <clocale>
int main(int argc,char** argv){try{
    std::setlocale(LC_ALL, "");require(argc==3,"test arguments supplied");initModelTests(argv[2]);
    for(const auto& name:{L"与门",L"或门",L"非门",L"与非门",L"或非门",L"异或门",L"同或门",L"半加器",L"全加器"}){
        auto gate=component(name,10);int inputs=0,outputs=0;
        for(const auto& p:gate.pins){inputs+=p.dir==se::PinDir::Input;outputs+=p.dir==se::PinDir::Output;}
        for(int bits=0;bits<(1<<inputs);++bits){
            se::NetGraph g;std::vector<se::ComponentInstance> cs;
            for(int k=0;k<inputs;++k){cs.push_back(component(L"输入引脚",k+1));g.pinNet.push_back({k});}
            cs.push_back(gate);g.nets.resize(inputs+outputs);std::vector<int> mapping;for(int k=0;k<inputs+outputs;++k)mapping.push_back(k);g.pinNet.push_back(mapping);
            se::Simulator sim;sim.Reset(cs.size());int sum=0;
            for(int k=0;k<inputs;++k){int v=(bits>>k)&1;sum+=v;sim.SetInputValue(k,v?se::LogicValue::H:se::LogicValue::L);}
            require(sim.Evaluate(cs,g),"combinational evaluation finishes");
            const wxString n=name;int expected=n==L"与门"?sum==inputs:n==L"与非门"?sum!=inputs:n==L"或门"?sum>0:n==L"或非门"?sum==0:n==L"非门"?!sum:n==L"同或门"?!(sum%2):sum%2;
            require(sim.NetValue(inputs)==(expected?se::LogicValue::H:se::LogicValue::L),"builtin truth table output");
            if(outputs==2)require(sim.NetValue(inputs+1)==(sum>=2?se::LogicValue::H:se::LogicValue::L),"adder carry output");
            for(int k=0;k<inputs;++k)cs[k].value=((bits>>k)&1)?"1":"0";
            se::DigitalSimulator timedTruth;wxString reason;
            require(timedTruth.Reset(cs,g,reason),"event-driven builtin initializes");timedTruth.Advance();
            require(timedTruth.Value(inputs)==expected,"event-driven builtin truth output");
            if(outputs==2)require(timedTruth.Value(inputs+1)==(sum>=2?1:0),"event-driven adder carry");
        }
    }
    // Independent timed chain: input -> inverter (2 ticks) -> inverter (3 ticks).
    auto source=component(L"输入引脚",11);source.value="0";
    auto first=component(L"非门",22),second=component(L"非门",33);
    first.delayTicks=2;second.delayTicks=3;
    std::vector<se::ComponentInstance> chain{source,first,second};
    se::NetGraph graph;graph.nets.resize(3);graph.pinNet={{0},{0,1},{1,2}};
    se::DigitalSimulator timed;wxString error;
    require(timed.Reset(chain,graph,error),"timed chain initializes");
    require(timed.Value(0)==0&&timed.Value(1)==-1&&timed.Value(2)==-1,"initial unknowns do not settle instantly");
    timed.Advance();require(timed.Value(1)==-1,"first gate does not propagate before its delay");
    timed.Advance();require(timed.Value(1)==1&&timed.Value(2)==-1,"first gate propagates at exact tick two");
    timed.Advance(2);require(timed.Value(2)==-1,"second gate retains its independent delay");
    timed.Advance();require(timed.Value(2)==0&&timed.Time()==5,"two gate delays accumulate to five ticks");
    require(timed.SetInput(11,1)&&!timed.SetInput(0,1),"input events use stable component IDs");
    timed.Advance(2);require(timed.Value(1)==0&&timed.Value(2)==0,"downstream gate waits after live input change");
    timed.Advance(3);require(timed.Value(2)==1,"live input reaches final output after path delay");
    timed.SetInput(11,0);timed.Advance();timed.SetInput(11,1);timed.Advance(5);
    require(timed.Value(1)==0&&timed.Value(2)==1,"inertial delay cancels a pulse shorter than gate delay");
    auto clock=component(L"时钟",44);clock.clockPeriodTicks=4;
    se::NetGraph cg;cg.nets.resize(1);cg.pinNet={{0}};
    require(timed.Reset({clock},cg,error)&&timed.Value(0)==0,"clock starts low");
    timed.Advance();require(timed.Value(0)==0,"clock stays low before half period");
    timed.Advance();require(timed.Value(0)==1,"automatic clock rises at half period");
    timed.Advance(2);require(timed.Value(0)==0,"automatic clock falls at full period");
    auto high=source;high.id=99;high.value="1";source.value="0";
    se::NetGraph conflict;conflict.nets.resize(1);conflict.pinNet={{0},{0}};
    require(timed.Reset({source,high},conflict,error)&&timed.Value(0)==2,"conflicting drivers are reported explicitly");
    auto custom=component(L"与门",3);custom.name=L"自定义逻辑";custom.logicModel="truth";custom.truthTable="0110";custom.delayTicks=4;
    auto a=source,b=source;a.id=1;b.id=2;a.value="1";b.value="0";
    se::NetGraph tg;tg.nets.resize(3);tg.pinNet={{0},{1},{0,1,2}};
    se::SchematicDocument doc;doc.components={a,b,custom};
    const auto path=wxFileName(wxString::FromUTF8(argv[2]),"temporal-roundtrip.ses").GetFullPath();
    require(se::SchematicIO::Save(path,doc,&error),"save custom logic and timing");
    se::SchematicDocument loaded;require(se::SchematicIO::Load(path,loaded,&error),"reopen custom logic and timing");
    require(loaded.components[2].logicModel=="truth"&&loaded.components[2].truthTable=="0110"&&loaded.components[2].delayTicks==4,"instance model and delay survive roundtrip");
    require(timed.Reset(loaded.components,tg,error),"reopened custom logic initializes");timed.Advance(3);
    require(timed.Value(2)==-1,"custom logic respects delay after reopening");timed.Advance();
    require(timed.Value(2)==1,"custom XOR truth table executes after reopening");
    custom.truthTable="01";require(!timed.Reset({a,b,custom},tg,error),"malformed custom logic is rejected");
    se::ComponentDef def;def.name=L"自定义异或";def.refPrefix="U";def.inputs=2;def.outputs=1;def.w=60;def.h=40;
    def.logicModel="truth";def.truthTable="0110";def.delayTicks=4;def.clockPeriodTicks=12;
    se::CategoryDef category;category.name=L"用户自定义";category.items={def};
    require(se::ComponentCatalog::SaveUserLibrary({category},&error)&&se::ComponentCatalog::Reload(&error),"custom logic library saves and reloads");
    const auto* restored=se::ComponentCatalog::Find(def.name);
    require(restored&&restored->truthTable=="0110"&&restored->delayTicks==4&&restored->clockPeriodTicks==12,"library preserves truth table and timing");
    def.truthTable="bad";category.items={def};require(!se::ComponentCatalog::SaveUserLibrary({category},&error),"invalid library truth table cannot overwrite saved data");
    require(se::ComponentCatalog::Reload(&error)&&se::ComponentCatalog::Find(def.name)->truthTable=="0110","rejected save preserves previous library");
    se::Simulator facade;require(facade.StartRuntime(chain,graph,error),"runtime facade starts");
    facade.AdvanceRuntime();auto t=facade.RuntimeTime();facade.SetRunning(false);
    require(facade.RuntimeTime()==t&&facade.RuntimeReady(),"pause preserves timeline state");
    facade.Reset(0);require(!facade.RuntimeReady(),"topology reset invalidates all queued events");
    std::cout<<"Executed steady-state truth tables and timed event propagation scenarios.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
