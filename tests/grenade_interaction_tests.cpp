#include "vr/vr_grenade_interaction.h"
#include "vr/vr_interactions.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
namespace grenade = kisak::vr::grenade_interaction;
namespace interactions = kisak::vr::interactions;
int checks = 0;
int failures = 0;
void Expect(bool result, const char* message)
{
    ++checks;
    if (!result) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }
struct Quaternion { float x, y, z, w; };
Quaternion Multiply(Quaternion a, Quaternion b)
{
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
Quaternion Axis(int axis, float degrees)
{
    const float half = degrees * 3.14159265358979323846f / 360.0f;
    Quaternion q{0,0,0,std::cos(half)};
    if (axis == 0) q.x = std::sin(half);
    if (axis == 1) q.y = std::sin(half);
    if (axis == 2) q.z = std::sin(half);
    return q;
}
void Rotate(Quaternion q, const float v[3], float result[3])
{
    const Quaternion rotated = Multiply(Multiply(q, {v[0],v[1],v[2],0}),
                                       {-q.x,-q.y,-q.z,q.w});
    result[0]=rotated.x; result[1]=rotated.y; result[2]=rotated.z;
}
grenade::GripUpdate Grip(grenade::GripState& state, bool held, bool available,
                         bool validPose, bool holding, std::uint32_t now)
{
    return grenade::UpdateGrip(&state, held, available, validPose, holding, now);
}
void Geometry()
{
    constexpr float units = 39.37007874f;
    const float forward[3] = {0,0,-1};
    for (float yaw : {-180.0f,-90.0f,-30.0f,0.0f,45.0f,120.0f,180.0f})
    for (float pitch : {-80.0f,-60.0f,-45.0f,0.0f,45.0f,60.0f,80.0f})
    for (float roll : {-75.0f,-30.0f,0.0f,30.0f,75.0f})
    for (float side : {-13.0f,13.0f})
    {
        // Hip follows body yaw, not the direction in which the head tilts.
        const float bodyOffset[3] = {-side / units,-28.0f / units,0};
        float trackingOffset[3], headForward[3], position[3];
        Rotate(Axis(1,yaw), bodyOffset, trackingOffset);
        Rotate(Multiply(Multiply(Axis(1,yaw),Axis(0,pitch)),Axis(2,roll)),
               forward, headForward);
        Expect(grenade::ProjectLevelBeltPosition(trackingOffset,headForward,units,position),
               "yaw/pitch/roll case must have a valid level heading");
        Expect(Near(position[0],0) && Near(position[1],side) && Near(position[2],-28),
               "stationary hip must keep the same level-belt coordinates");
        const auto zones = grenade::FindBeltZones(position,true);
        Expect(zones.left == (side > 0) && zones.right == (side < 0),
               "both hips must stay selectable through head pitch and roll");
        for (auto dominant : {interactions::DominantHand::Left,interactions::DominantHand::Right})
        for (auto layout : {interactions::GrenadeBeltLayout::Fixed,interactions::GrenadeBeltLayout::Handed})
        {
            const bool fragLeft = interactions::FragUsesLeftHip(dominant,layout);
            Expect((zones.left == fragLeft) ==
                       (side > 0 ? fragLeft : !fragLeft),
                   "physical side must preserve the existing handed/fixed slot mapping");
        }
    }
    for (float pitch : {45.0f,60.0f,75.0f})
    {
        const float angle = pitch * 3.14159265358979323846f / 180.0f;
        const float oldHeadLocal[3] = {28*std::sin(angle),13,-28*std::cos(angle)};
        Expect(!grenade::FindBeltZones(oldHeadLocal,true).left,
               "regression fixture must demonstrate old head-pitched hip rejection");
    }
    const float boundaries[][3] = {{-18,2,-42},{18,24,-14},{-18,-2,-42},{18,-24,-14}};
    for (const auto& point : boundaries)
    {
        const auto zones=grenade::FindBeltZones(point,true);
        Expect(zones.left != zones.right,"existing inclusive belt bounds must remain selectable");
        Expect(!grenade::FindBeltZones(point,false).left && !grenade::FindBeltZones(point,false).right,
               "invalid tracking must not make a selectable belt");
    }
    const float outside[][3] = {{-18.01f,13,-28},{18.01f,13,-28},{0,1.99f,-28},
        {0,24.01f,-28},{0,13,-42.01f},{0,13,-13.99f},{0,0,-28}};
    for(const auto& point:outside)
    {
        const auto zones=grenade::FindBeltZones(point,true);
        Expect(!zones.left && !zones.right,"outside and central-gap points must stay rejected");
    }
    grenade::BeltSettings custom{5,-20,10,4};
    const float customPoint[3]={5,-10,-20};
    Expect(grenade::FindBeltZones(customPoint,true,custom).right,"custom belt dimensions must apply");
    custom.radius=10;
    Expect(!grenade::FindBeltZones(customPoint,true,custom).right,"overlapping hip zones fail closed");
    float position[3]; const float offset[3]={-.3f,-.7f,0};
    const float vertical[3]={0,-1,0};
    Expect(!grenade::ProjectLevelBeltPosition(offset,vertical,units,position),
           "vertical heading must not invent a belt side");
    Expect(!grenade::ProjectLevelBeltPosition(nullptr,forward,units,position),"null offset rejected");
    Expect(!grenade::ProjectLevelBeltPosition(offset,forward,units,nullptr),"null output rejected");
    Expect(!grenade::ProjectLevelBeltPosition(offset,forward,0,position),"invalid units rejected");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    for(int component=0;component<3;++component)
    {
        float bad[3]={0,0,-1}; bad[component]=nan;
        Expect(!grenade::ProjectLevelBeltPosition(offset,bad,units,position),"nonfinite heading rejected");
        Expect(!grenade::ProjectLevelBeltPosition(bad,forward,units,position),"nonfinite position rejected");
        Expect(!grenade::FindBeltZones(bad,true).left,"nonfinite belt rejected");
    }
}
void GripTracking()
{
    grenade::GripState state;
    Expect(!Grip(state,true,true,true,false,0).grabPressed,"held-at-start must not grab");
    Expect(!Grip(state,true,true,true,false,10).grabPressed,"holding cannot retrigger");
    Grip(state,false,true,true,false,20);
    Expect(Grip(state,true,true,true,false,30).grabPressed,"fresh available press must grab");
    Expect(!Grip(state,true,true,false,true,40).release,"pose loss alone must not release");
    Expect(!Grip(state,true,true,false,true,5000).release,"prolonged pose loss with grip held is not a release");
    Expect(!Grip(state,true,true,true,true,5010).release,"pose recovery while held must not release");
    Expect(Grip(state,false,true,true,true,5020).release,"valid real release is immediate");
    Expect(!Grip(state,false,true,true,false,5030).release,"released stage must not emit twice");

    state={}; Grip(state,false,true,true,false,0);
    Expect(!Grip(state,true,true,false,false,10).grabPressed,"press without pose is consumed, not queued");
    Expect(!Grip(state,true,true,true,false,20).grabPressed,"pose recovery cannot synthesize pickup");
    Grip(state,false,true,true,false,30);
    Expect(Grip(state,true,true,true,false,40).grabPressed,"release/repress after loss rearms pickup");

    state={}; Grip(state,false,true,true,false,0);
    Expect(!Grip(state,false,false,true,false,10).grabPressed,"inactive action is not an available release");
    Expect(!Grip(state,true,true,true,false,20).grabPressed,"action recovery held cannot synthesize pickup");
    Grip(state,false,true,true,false,30);
    Expect(Grip(state,true,true,true,false,40).grabPressed,"new press after action recovery is accepted");
    Expect(!Grip(state,false,false,false,true,50).release,"runtime-inactive false state cannot throw");
    Expect(!Grip(state,false,false,true,true,5000).release,"inactive action never times into fake release");
    Expect(!Grip(state,true,true,true,true,5010).release,"recovered held action preserves held grenade");
    Expect(Grip(state,false,true,true,true,5020).release,"actual available release still throws");

    // The helper receives resolved object hold/toggle state, so physical button
    // release while toggled on must not throw; the second toggle does throw.
    state={}; Grip(state,false,true,true,false,0);
    Expect(Grip(state,true,true,true,false,1).grabPressed,"toggle-on grabs");
    Expect(!Grip(state,true,true,true,true,2).release,"physical release with toggle still on preserves hold");
    Expect(Grip(state,false,true,true,true,3).release,"toggle-off releases");

    for(std::uint32_t start:{0u,100u,0xfffffff0u})
    {
        state={}; Grip(state,true,true,true,false,start-1u);
        Expect(!Grip(state,false,true,false,true,start).release,"real release waits for fresh pose");
        Expect(!Grip(state,false,true,false,true,start+149u).release,"release wait is bounded but not premature");
        auto update=Grip(state,false,true,true,true,start+149u);
        Expect(update.release && !update.lastValidPoseFallback,"fresh pose releases immediately before timeout");

        state={}; Grip(state,true,true,true,false,start-1u);
        Grip(state,false,true,false,true,start);
        update=Grip(state,false,true,false,true,start+150u);
        Expect(update.release && update.lastValidPoseFallback,"150ms timeout uses last valid pose only after real release");

        state={}; Grip(state,true,true,true,false,start-1u);
        Grip(state,false,true,false,true,start);
        update=Grip(state,true,true,true,true,start+10u);
        Expect(update.release,"regrip does not cancel a confirmed deferred release");
        Expect(!Grip(state,true,true,true,false,start+11u).grabPressed,"regrip while release pending cannot auto-grab next grenade");

        state={}; Grip(state,true,true,true,false,start-1u);
        Grip(state,false,true,false,true,start);
        update=Grip(state,false,false,false,true,start+150u);
        Expect(update.release && update.lastValidPoseFallback,"later action loss cannot erase already-confirmed release");
    }
    state={}; grenade::ObserveDisabledGrip(&state,true,true);
    Expect(!Grip(state,true,true,true,false,0).grabPressed,"reenabling while held must not grab");
    grenade::ObserveDisabledGrip(&state,false,false);
    Expect(!Grip(state,true,true,true,false,1).grabPressed,"disabled unavailable state must rearm safely");
    Expect(!grenade::UpdateGrip(nullptr,false,true,true,false,0).release,"null tracker fails safely");
}
void BindingAvailability()
{
    grenade::BindingState bindings;
    grenade::GripState grip;
    auto sample = [&](std::uint32_t known, std::uint32_t held, bool holding,
                      std::uint32_t now)
    {
        const auto binding=grenade::ResolveObjectBindings(&bindings,3,known,held);
        return Grip(grip,binding.held,binding.available,true,holding,now);
    };
    // A permanently unsupported alternative cannot make the usable primary
    // look like a fresh recovery every time it is pressed.
    sample(1,0,false,0);
    Expect(sample(1,1,false,1).grabPressed,"supported primary works with unsupported alternate");
    Expect(sample(1,0,true,2).release,"supported primary releases with unsupported alternate");
    Expect(sample(1,1,false,3).grabPressed,"supported primary remains usable on repeated grab");
    Expect(!sample(2,0,true,4).release,"losing held primary cannot release via neutral available alternate");
    Expect(!sample(2,0,true,5000).release,"unknown held owner cannot time out as fake release");
    Expect(!sample(3,1,true,5001).release,"held primary recovery preserves grenade");
    Expect(sample(3,0,true,5002).release,"recovered owner reports real release");
    // Reappearing held alternate was never an accepted owner and cannot grab.
    bindings={}; grip={}; sample(1,0,false,0);
    Expect(!sample(3,2,false,1).grabPressed,"newly available held alternate cannot manufacture pickup");
    sample(3,0,false,2);
    Expect(sample(3,2,false,3).grabPressed,"available alternate rearms after a real neutral state");
    Expect(!sample(1,0,true,4).release,"alternate owner loss cannot be masked by available primary");
    Expect(sample(3,0,true,5).release,"alternate owner's confirmed release remains usable");
    // Two valid held owners implement OR: one released slot cannot release
    // the grenade while the other remains held or is temporarily unknown.
    bindings={}; grip={}; sample(3,0,false,0);
    Expect(sample(3,1,false,1).grabPressed,"primary grabs after both slots neutral");
    Expect(!sample(3,3,true,2).release,"second held alternative preserves grenade");
    Expect(!sample(3,2,true,3).release,"first released alternative does not release second owner");
    Expect(sample(3,0,true,4).release,"all confirmed released alternatives release grenade");
    // Returning owner released is a known release, even if another never-held
    // alternative remains unsupported throughout.
    bindings={}; grip={}; sample(1,0,false,0); sample(1,1,false,1);
    Expect(!sample(0,0,true,2).release,"all sources becoming unavailable cannot release");
    Expect(sample(1,0,true,3).release,"active false owner state after loss is a genuine release");
    bindings={};
    auto binding=grenade::ResolveObjectBindings(&bindings,1,1,1);
    Expect(!binding.held,"startup-held source must first be released");
    grenade::ResolveObjectBindings(&bindings,1,1,0);
    binding=grenade::ResolveObjectBindings(&bindings,1,1,1);
    Expect(binding.held && binding.available,"source works after startup neutral");
    binding=grenade::ResolveObjectBindings(&bindings,0,0,0);
    Expect(!binding.held && !binding.available,"unbound configuration has no held input");

    for(int a=0;a<3;++a) for(int b=0;b<3;++b)
    {
        grenade::ChordAvailability chord;
        // 0 unknown, 1 known false, 2 known true.
        chord.Observe(a!=0,a==2); chord.Observe(b!=0,b==2);
        Expect(chord.Known() == (a==1 || b==1 || (a==2 && b==2)),
               "chord availability must use three-valued AND, not availability AND");
    }
}
} // namespace

int main()
{
    Geometry(); GripTracking(); BindingAvailability();
    std::printf("Grenade interaction tests: %d checks, %d failures.\n",checks,failures);
    return failures ? 1 : 0;
}
