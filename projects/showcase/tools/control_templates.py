"""范例角色与参数化镜头的源配置；字段直接对应运行时 Schema。"""
def scalar(value=0, parameter="", scale=1, offset=0):
    return {"constant": value, "parameter": parameter, "scale": scale, "offset": offset}

def character_settings(camera_id, speed=4, jump_speed=5):
    return {
        "camera_node_id": str(camera_id),
        "movement": {"space": "view", "max_speed": speed, "ground_acceleration": 30,
                     "ground_braking": 40, "air_acceleration": 8, "air_braking": 0,
                     "air_speed_limit": speed, "inherit_support_planar_velocity": True},
        "facing": {"mode": "movement", "turn_speed": 540},
        "actions": [{"name": "jump", "buffer_seconds": 0.15},
                    {"name": "sprint", "buffer_seconds": 0}, {"name": "crouch", "buffer_seconds": 0}],
        "abilities": [{"id": "crouch", "type": "crouch", "enabled": True, "priority": 30},
                      {"id": "jump", "type": "jump", "enabled": True, "priority": 20},
                      {"id": "sprint", "type": "sprint", "enabled": True, "priority": 10}],
        "ability_data": {
            "crouch": [{"id": "crouch", "action": "crouch", "half_height": 0.3, "speed": 1.8}],
            "jump": [{"id": "jump", "action": "jump", "launch_delta_speed": jump_speed,
                      "coyote_seconds": 0.1, "hold_seconds": 0.18, "held_gravity_scale": 0.7,
                      "released_gravity_scale": 1.6, "allow_crouched": False}],
            "sprint": [{"id": "sprint", "action": "sprint", "speed": speed * 1.7}]},
    }

def camera_settings(subject_id, distance=4.6, height=1.55, yaw=0, pitch=0, fov=55, near=0.08, far=100,
                    radius=0.15, margin=0.02, first_person=False):
    def parameter(name, value, low=None, high=None):
        kind="bool_value" if isinstance(value,bool) else "float_value"
        out={"name":name,"type":kind,"default_value":{"name":name,"type":kind,kind:value}}
        if low is not None:out.update(has_range=True,minimum=low,maximum=high)
        return out
    modules={"lens":[{"id":"1","projection":"perspective","fov":scalar(fov),
                       "near_clip":scalar(near),"far_clip":scalar(far),"orthographic_size":scalar(5)}],
             "orbit":[],"collision":[{"id":"4","subject_slot":"subject","height_offset":scalar(parameter="height"),
                 "radius":radius,"margin":margin,"sight_enabled":True,"motion_enabled":False,
                 "include_characters":False,"recovery_damping":0.15,"recovery_delay":0.05}]}
    for id,dist in [(2,distance),(3,0)]:
        modules["orbit"].append({"id":str(id),"follow_slot":"subject","height_offset":scalar(parameter="height"),
            "distance":scalar(dist),"yaw":scalar(parameter="yaw"),"pitch":scalar(parameter="pitch"),
            "pitch_min":-80,"pitch_max":80,"shoulder_offset":scalar()})
    return {
        "parameters":[parameter("yaw",yaw,-180,180),parameter("pitch",pitch,-80,80),
                      parameter("first_person",first_person),parameter("height",height,0.1,10)],
        "targets":[{"name":"subject","node_local_id":str(subject_id)}],
        "rigs":[{"id":name,"modules":[{"id":"1","type":"lens"},{"id":str(id),"type":"orbit"}]} for name,id in [("third",2),("first",3)]],
        "module_data":modules,"output_modules":[{"id":"4","type":"collision"}],
        "machine":{"initial_state":"first" if first_person else "third",
            "states":[{"id":"third","rig":"third"},{"id":"first","rig":"first"}],
            "transitions":[{"id":"to-"+name,"from":other,"to":name,"interrupt":True,"blend":{"seconds":0.2,"curve":"smoothstep"},
                            "any_of":[{"all_of":[{"id":str(id),"type":"parameter"}]}]} for id,name,other in [(1,"first","third"),(2,"third","first")]],
            "condition_data":{"parameter_tests":[{"id":str(id),"parameter":"first_person","comparison":"equal",
                               "value":{"type":"bool_value","bool_value":value}} for id,value in [(1,True),(2,False)]]}}
    }

def adapter_settings(pointer_scale=0.12, eye_offset=-0.25):
    return {"subject_slot":"subject","yaw_parameter":"yaw","pitch_parameter":"pitch",
            "first_person_parameter":"first_person","first_person_states":["first"],
            "pointer_scale_degrees":{"x":pointer_scale,"y":pointer_scale},
            "rate_degrees_per_second":{"x":96,"y":96},
            "status_bindings":[{"source":"active_height","parameter":"height","offset":eye_offset}]}
