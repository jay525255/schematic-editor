#include "TestSupport.h"
#include <clocale>
int main(int argc,char** argv){try{
    std::setlocale(LC_ALL, "");require(argc==3,"test arguments supplied");initModelTests(argv[2]);
    auto inPin=[](int id){auto c=component(L"输入引脚",id);c.value="0";return c;};
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
    require(se::LogicValueText(-1)=="X"&&se::LogicValueText(0)=="0"&&se::LogicValueText(1)=="1"&&se::LogicValueText(2)=="ERR","portable logic-state display includes explicit conflicts");
    // Independent timed chain: input -> inverter (2 ticks) -> inverter (3 ticks).
    auto source=inPin(11);source.value="0";
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
    auto a=source,b=source;a.id=1;b.id=2;a.refdes="IN1";b.refdes="IN2";a.value="1";b.value="0";
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
    const auto* kept=se::ComponentCatalog::Find(def.name);
    require(se::SchematicIO::WriteTextFile(se::ComponentCatalog::GetLibraryPath(),"<wrong-root/>",&error),"prepare invalid library root");
    require(!se::ComponentCatalog::Reload(&error)&&se::ComponentCatalog::Find(def.name)==kept&&kept->truthTable=="0110","failed library reload preserves definitions and UI pointers");
    se::Simulator facade;require(facade.StartRuntime(chain,graph,error),"runtime facade starts");
    facade.AdvanceRuntime();auto t=facade.RuntimeTime();facade.SetRunning(false);
    require(facade.RuntimeTime()==t&&facade.RuntimeReady(),"pause preserves timeline state");
    facade.Reset(0);require(!facade.RuntimeReady(),"topology reset invalidates all queued events");
    // ---- sequential elements: flip-flops, counter, ROM, RAM ----
    {
        auto inD=inPin(1),inClk=inPin(2),dff=component(L"D 触发器",3);
        std::vector<se::ComponentInstance> cs{inD,inClk,dff};
        se::NetGraph g;g.nets.resize(4);g.pinNet={{0},{1},{0,1,2,3}};
        se::DigitalSimulator s;wxString err;
        if(!s.Reset(cs,g,err)){std::cout<<"RESET-ERROR "<<err<<std::endl;}
        require(s.Reset(cs,g,err),"D flip-flop initializes");
        require(s.Value(2)==0&&s.Value(3)==1,"D flip-flop powers up cleared");
        s.SetInput(1,1);s.Advance(2);
        require(s.Value(2)==0,"D flip-flop ignores D without a clock edge");
        s.SetInput(2,1);s.Advance(2);
        require(s.Value(2)==1&&s.Value(3)==0,"D flip-flop samples D=1 on the rising edge");
        s.SetInput(2,0);s.Advance(2);
        require(s.Value(2)==1,"D flip-flop holds through the falling edge");
        s.SetInput(1,0);s.Advance(1);s.SetInput(2,1);s.Advance(2);
        require(s.Value(2)==0&&s.Value(3)==1,"D flip-flop samples D=0 on the next edge");
    }
    {
        auto inJ=inPin(1),inK=inPin(2),inC=inPin(3),jk=component(L"JK 触发器",4);
        std::vector<se::ComponentInstance> cs{inJ,inK,inC,jk};
        se::NetGraph g;g.nets.resize(5);g.pinNet={{0},{1},{2},{0,1,2,3,4}};
        se::DigitalSimulator s;wxString err;
        require(s.Reset(cs,g,err),"JK flip-flop initializes");
        s.SetInput(1,1);s.Advance(1);s.SetInput(3,1);s.Advance(2);
        require(s.Value(3)==1&&s.Value(4)==0,"JK flip-flop sets on a rising edge");
        s.SetInput(3,0);s.Advance(1);s.SetInput(1,0);s.SetInput(2,1);s.Advance(1);s.SetInput(3,1);s.Advance(2);
        require(s.Value(3)==0&&s.Value(4)==1,"JK flip-flop resets on a rising edge");
        s.SetInput(3,0);s.Advance(1);s.SetInput(2,0);s.Advance(1);s.SetInput(3,1);s.Advance(2);
        require(s.Value(3)==0,"JK flip-flop holds when both inputs are low");
        s.SetInput(3,0);s.Advance(1);s.SetInput(1,1);s.SetInput(2,1);s.Advance(1);s.SetInput(3,1);s.Advance(2);
        require(s.Value(3)==1,"JK flip-flop toggles when both inputs are high");
    }
    {
        auto inC=inPin(1),inR=inPin(2),cnt=component(L"4 位计数器",3);
        std::vector<se::ComponentInstance> cs{inC,inR,cnt};
        se::NetGraph g;g.nets.resize(6);g.pinNet={{0},{1},{0,1,2,3,4,5}};
        se::DigitalSimulator s;wxString err;
        require(s.Reset(cs,g,err),"4-bit counter initializes");
        require(s.Value(2)==0&&s.Value(3)==0&&s.Value(4)==0&&s.Value(5)==0,"4-bit counter starts at zero");
        for(int i=0;i<3;++i){s.SetInput(1,1);s.Advance(1);s.SetInput(1,0);s.Advance(1);}
        require(s.Value(2)==1&&s.Value(3)==1&&s.Value(4)==0&&s.Value(5)==0,"three clock pulses count to binary 0011");
        s.SetInput(2,1);s.Advance(1);
        require(s.Value(2)==0&&s.Value(3)==0&&s.Value(4)==0,"asynchronous reset clears the counter");
    }
    {
        auto a0=inPin(1),a1=inPin(2),a2=inPin(3),a3=inPin(4),rom=component(L"ROM 16×4",5);
        std::vector<se::ComponentInstance> cs{a0,a1,a2,a3,rom};
        se::NetGraph g;g.nets.resize(8);g.pinNet={{0},{1},{2},{3},{0,1,2,3,4,5,6,7}};
        se::DigitalSimulator s;wxString err;
        require(s.Reset(cs,g,err),"ROM initializes with its factory image");s.Advance(2);
        require(s.Value(4)==0&&s.Value(5)==0&&s.Value(6)==0&&s.Value(7)==0,"ROM address 0 reads 0000");
        s.SetInput(1,1);s.SetInput(2,1);s.Advance(2);
        require(s.Value(4)==1&&s.Value(5)==1&&s.Value(6)==0&&s.Value(7)==0,"ROM address 3 reads 0011");
        s.SetInput(1,0);s.SetInput(2,0);s.SetInput(3,1);s.Advance(2);
        require(s.Value(4)==0&&s.Value(5)==0&&s.Value(6)==1&&s.Value(7)==0,"ROM address 4 reads 0100");
        s.SetInput(1,1);s.SetInput(4,1);s.Advance(2);
        require(s.Value(4)==1&&s.Value(5)==0&&s.Value(6)==1&&s.Value(7)==1,"ROM address 13 reads 1101");
    }
    {
        auto a0=inPin(1),a1=inPin(2),a2=inPin(3),a3=inPin(4);
        auto we=inPin(5),d0=inPin(6),d1=inPin(7),d2=inPin(8),d3=inPin(9);
        auto ram=component(L"RAM 16×4",10);
        std::vector<se::ComponentInstance> cs{a0,a1,a2,a3,we,d0,d1,d2,d3,ram};
        se::NetGraph g;g.nets.resize(13);g.pinNet={{0},{1},{2},{3},{4},{5},{6},{7},{8},{0,1,2,3,4,5,6,7,8,9,10,11,12}};
        se::DigitalSimulator s;wxString err;
        require(s.Reset(cs,g,err),"RAM initializes empty");s.Advance(2);
        require(s.Value(9)==0&&s.Value(10)==0&&s.Value(11)==0&&s.Value(12)==0,"RAM address 0 holds its factory value 0000");
        s.SetInput(2,1);s.Advance(2);
        require(s.Value(9)==0&&s.Value(10)==1&&s.Value(11)==0&&s.Value(12)==0,"RAM address 2 still holds its factory value 0010");
        s.SetInput(6,1);s.SetInput(8,1);s.SetInput(5,1);s.Advance(2);
        require(s.Value(9)==1&&s.Value(10)==0&&s.Value(11)==1&&s.Value(12)==0,"RAM keeps 0101 at the written address");
        s.SetInput(1,1);s.SetInput(5,0);s.Advance(2);
        require(s.Value(9)==1&&s.Value(10)==1&&s.Value(11)==0&&s.Value(12)==0,"RAM write only touches the selected address");
        s.SetInput(1,0);s.Advance(2);
        require(s.Value(9)==1&&s.Value(11)==1,"RAM keeps the stored word per address");
    }
    std::cout<<"Executed steady-state truth tables and timed event propagation scenarios.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

