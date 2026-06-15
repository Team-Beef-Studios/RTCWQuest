#if !defined(vr_client_info_h)
#define vr_client_info_h

#define NUM_WEAPON_SAMPLES      10
#define WEAPON_RECOIL           15.0f;

#define ACTIVE_OFF_HAND      1
#define ACTIVE_WEAPON_HAND   2

typedef struct {
    qboolean screen;
    float  fov;
    float  fov_x;
    float  fov_y;
    int    cgzoommode;
    int    eye;
    float  off_center_fov_x[2];
    float  off_center_fov_y[2];
    qboolean using_screen_layer;
    qboolean weapon_stabilised;
    qboolean right_handed;
    qboolean menu_right_handed;
    qboolean player_moving;
    qboolean visible_hud;
    qboolean cin_camera;
    int weaponid;
    int lastweaponid;
    int backpackitemactive; //0 - nothing, 1 - grenades, 2 - knife, 3 - Binoculars
    qboolean mountedgun;

    vec3_t hmdposition;
    vec3_t hmdposition_last; // Don't use this, it is just for calculating delta!
    vec3_t hmdposition_delta;
    vec3_t hmdposition_snap;
    vec3_t hmdposition_offset;

    vec3_t hmdorientation;
    vec3_t hmdorientation_last; // Don't use this, it is just for calculating delta!
    vec3_t hmdorientation_delta;
    vec3_t hmdorientation_snap;

    vec3_t weaponangles_knife;
    vec3_t weaponangles;
    vec3_t weaponangles_last; // Don't use this, it is just for calculating delta!
    vec3_t weaponangles_delta;
    vec3_t dominanthandangles;

    float weapon_recoil;            // recoil effect to improve the default

    vec3_t current_weaponoffset;
    vec3_t calculated_weaponoffset;
    float current_weaponoffset_timestamp;
    vec3_t weaponoffset_history[NUM_WEAPON_SAMPLES];
    float weaponoffset_history_timestamp[NUM_WEAPON_SAMPLES];

    qboolean pistol;                // True if the weapon is a pistol

    //Lots of scope weapon stuff
    qboolean scopeengaged;          // Scope has been engaged on a scoped weapon
    qboolean scopedweapon;          // Weapon scope is available
    qboolean scopedetached;         // Scope has been detached from weapon
    qboolean detachablescope;       // Scope can be detached from weapon

    qboolean hasbinoculars;

    qboolean velocitytriggered;  // Weapon attack triggered by velocity (knife)
    qboolean weaponstabilizable; // Flag if currently held weapon can be stabilized
    qboolean dualwielding;       // Flag if dual wielding is active

    vec3_t offhandweaponangles;
    vec3_t offhandangles;
    vec3_t offhandangles_last; // Don't use this, it is just for calculating delta!
    vec3_t offhandangles_delta;

    vec3_t offhandoffset;

    vec3_t clientviewangles; //orientation in the client - we use this in the cgame
    qboolean clientview_hmd_yaw_valid;
    float clientview_hmd_yaw;
    float clientview_yaw_last; // Don't use this, it is just for calculating delta!
    float clientview_yaw_delta;

    //
    // Teleport Stuff
    //
    qboolean teleportenabled;
    qboolean teleportseek; // player looking to teleport
    qboolean teleportready; // player pointing to a valid teleport location
    vec3_t   teleportdest; // teleport destination
    qboolean teleportexecute; // execute the teleport

    int      useGestureState;
    int      wheelSelectorEnabled;
    qboolean binocularsActive;
    qboolean useHoldableItem;
    qboolean toggleMainMenu;
    int      akimboTriggerState;
    qboolean akimboFire;
    qboolean vrIrlCrouchEnabled;
    float    vrIrlCrouchToStandRatio;
    float    viewHeight;
    float    maxHeight;
    float    curHeight;

    //////////////////////////////////////
    //    Test stuff for weapon alignment
    //////////////////////////////////////

    char    test_name[256];
    float   test_scale;
    vec3_t  test_angles;
    vec3_t  test_offset;

} vr_client_info_t;

#endif //vr_client_info_h
