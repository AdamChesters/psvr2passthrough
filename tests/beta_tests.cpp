#include "camera_geometry.h"
#include "config.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace psvr2pt;
void check(bool result,const char* name) { if(!result){std::cerr<<"FAIL: "<<name<<'\n'; std::exit(1);} }
bool near(float a,float b) {return std::abs(a-b)<1e-5f;}
int main() {
    float transform[3][4]={{1,0,0,-.04f},{0,1,0,-.03f},{0,0,1,-.09f}};
    CameraPose camera;
    check(camera_pose(transform,camera),"physical camera transform accepted");
    check(near(camera.position[0],-.04f)&&near(camera.orientation[3],1),"translation and identity orientation");
    const float s=std::sqrt(.5f);
    CameraPose head{{0,s,0,s},{1,2,3}};
    const auto combined=compose_pose(head,camera);
    check(near(combined.position[0],.91f)&&near(combined.position[1],1.97f)&&near(combined.position[2],3.04f),"head rotation also rotates camera translation");
    check(near(combined.orientation[1],s)&&near(combined.orientation[3],s),"camera orientation composed");
    for(int axis=0;axis<3;++axis) {
        float halfturn[3][4]={{-1,0,0,0},{0,-1,0,0},{0,0,-1,0}}; halfturn[axis][axis]=1;
        check(camera_pose(halfturn,camera),"180 degree camera transform");
        check(near(std::abs(camera.orientation[axis]),1)&&near(camera.orientation[3],0),"180 degree quaternion branch");
    }
    transform[0][0]=-1; check(!camera_pose(transform,camera),"reflection rejected");
    transform[0][0]=1;transform[0][1]=.2f;check(!camera_pose(transform,camera),"sheared calibration rejected");
    transform[0][1]=0;transform[0][3]=std::numeric_limits<float>::quiet_NaN();check(!camera_pose(transform,camera),"nonfinite calibration rejected");
    float projection[4][4]={{2,0,.2f,0},{0,3,-.3f,0},{0,0,-1,-.1f},{0,0,-1,0}};
    CameraFov f;
    check(camera_fov(projection,f),"asymmetric projection accepted");
    check(near(std::tan(f.left),-.4f)&&near(std::tan(f.right),.6f)&&near(std::tan(f.up),.7f/3)&&near(std::tan(f.down),-1.3f/3),"frustum matches projection at all four edges");
    projection[0][0]=0;check(!camera_fov(projection,f),"zero focal scale rejected");
    projection[0][0]=2;projection[3][2]=1;check(!camera_fov(projection,f),"wrong handedness rejected");
    Config config;
    check(config_from_json(R"({"pt_type":1,"pt_vk":117,"toggle_mode":true,"global_alpha":0.7,"camera_toe_out_rad_l":0.3,"brightness":4,"reprojection_enabled":false})",config),"legacy configuration imports");
    check(config.toggle_mode&&config.passthrough_binding.vk_code==117&&near(config.global_alpha,.7f),"binding opacity and mode preserved");
    auto encoded=config_to_json(config);
    check(encoded.find("camera_toe")==std::string::npos&&encoded.find("brightness")==std::string::npos&&encoded.find("reprojection")==std::string::npos,"removed controls not serialized");
    config.passthrough_binding.type=BindingType::DirectInput;
    config.passthrough_binding.dinput_device_guid="{12345678-1234-1234-1234-123456789012}";
    config.passthrough_binding.dinput_button_index=127;
    config.passthrough_binding.dinput_device_name="HOTAS \\\"Test\\\"\n\\device";
    Config roundtrip;
    check(config_from_json(config_to_json(config),roundtrip),"HOTAS config roundtrip");
    check(config_to_json(roundtrip)==config_to_json(config),"escaped device name preserved");
    const auto before=config_to_json(config);
    check(!config_from_json("{",config)&&config_to_json(config)==before,"malformed JSON preserves existing config");
    check(!config_from_json(R"({"global_alpha":"bright"})",config),"wrong setting type rejected");
    check(!config_from_json(R"({"pt_type":3,"pt_di_guid":"id","pt_di_btn":128})",config),"invalid HOTAS button rejected");
    check(!config_from_json(R"({"pt_type":2,"pt_xi_btn":-1})",config),"negative XInput mask rejected");
    check(config_from_json(R"({"global_alpha":2})",config)&&config.global_alpha==1,"opacity clamped");
    check(config_from_json("{}",config)&&config.passthrough_binding.is_none()&&!config.force_passthrough_on,"default binding stays hidden");
    std::cout<<"All beta geometry and configuration tests passed\n";
}
