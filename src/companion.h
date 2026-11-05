#pragma once
#include <random>
#include <string>
namespace pet {
struct CompanionIntent {
    std::string action="idle";
    bool resting=false;
    int direction=0;
};
class Companion {
    std::mt19937 random;
    CompanionIntent intent;
    std::string selectedAction;
    double nextChange=12, nextRest=90, restUntil=0;
    int previousChoice=-1;
public:
    explicit Companion(unsigned seed=0x706574):random(seed) {}
    bool holding() const { return !selectedAction.empty(); }
    const std::string& heldAction() const { return selectedAction; }
    void hold(std::string action,double t) { selectedAction=std::move(action); interact(t); }
    void resume(double t) { selectedAction.clear(); interact(t); }
    void interact(double t) {
        intent={selectedAction.empty()?"idle":selectedAction,false,0}; nextChange=t+12; nextRest=t+90; restUntil=0;
    }
    CompanionIntent update(double t,bool available,bool wander) {
        // Interactions and flight take precedence. Suppression does not advance
        // the sequence, so returning from a menu cannot cause a burst of actions.
        if(!available) return intent;
        if(holding()) return {selectedAction,false,0};
        if(restUntil>0) {
            if(t<restUntil) return intent;
            restUntil=0; intent={"waving",false,0}; nextChange=t+2.8;
            nextRest=t+100+random()%50; return intent;
        }
        if(t>=nextRest) {
            intent={"idle",true,0}; restUntil=t+18+random()%15; return intent;
        }
        if(t<nextChange) return intent;
        if(intent.action!="idle") {
            intent={}; nextChange=t+10+random()%13; return intent;
        }
        int choice=(int)(random()%(wander?5:3));
        if(choice==previousChoice) choice=(choice+1)%(wander?5:3);
        previousChoice=choice;
        switch(choice) {
        case 0: intent={"review",false,0}; break;
        case 1: intent={"running",false,0}; break;
        case 2: intent={"waving",false,0}; break;
        case 3: intent={"running-left",false,-1}; break;
        default: intent={"running-right",false,1}; break;
        }
        nextChange=t+(intent.direction?3.:intent.action=="waving"?2.5:5.);
        return intent;
    }
};
}
