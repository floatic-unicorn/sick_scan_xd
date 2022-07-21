#include <exception>
#include <iomanip>
#include <memory>
#include <signal.h>
#include <sstream>
#include <string>
#include <vector>

#include "sick_scan_api.h"
#include "sick_scan_api_dump.h"
#include "sick_scan/sick_generic_laser.h"
#include <sick_scan/sick_generic_callback.h>

static std::string s_scannerName = "sick_scan";
static std::map<SickScanApiHandle,std::string> s_api_caller;
static std::vector<void*> s_malloced_resources;
static sick_scan::SickCallbackHandler<SickScanApiHandle,SickScanPointCloudMsg>          s_callback_handler_cartesian_pointcloud_messages;
static sick_scan::SickCallbackHandler<SickScanApiHandle,SickScanPointCloudMsg>          s_callback_handler_polar_pointcloud_messages;
static sick_scan::SickCallbackHandler<SickScanApiHandle,SickScanImuMsg>                 s_callback_handler_imu_messages;
static sick_scan::SickCallbackHandler<SickScanApiHandle,SickScanLFErecMsg>              s_callback_handler_lferec_messages;
static sick_scan::SickCallbackHandler<SickScanApiHandle,SickScanLIDoutputstateMsg>      s_callback_handler_lidoutputstate_messages;
static sick_scan::SickCallbackHandler<SickScanApiHandle,SickScanRadarScan>              s_callback_handler_radarscan_messages;
static sick_scan::SickCallbackHandler<SickScanApiHandle,SickScanLdmrsObjectArray>       s_callback_handler_ldmrsobjectarray_messages;
static sick_scan::SickCallbackHandler<SickScanApiHandle,SickScanVisualizationMarkerMsg> s_callback_handler_visualizationmarker_messages;

static SickScanApiHandle castNodeToApiHandle(rosNodePtr node)
{
    return ((SickScanApiHandle)(&(*node))); // return ((SickScanApiHandle)node);
}

static rosNodePtr castApiHandleToNode(SickScanApiHandle apiHandle)
{
    return (*((rosNodePtr*)&apiHandle)); // return ((rosNodePtr)apiHandle);
}

/*
*  Message converter
*/

static SickScanPointCloudMsg convertPointCloudMsg(const sick_scan::PointCloud2withEcho& msg_with_echo)
{
    SickScanPointCloudMsg export_msg;
    memset(&export_msg, 0, sizeof(export_msg));
    // Copy header and pointcloud dimension
    const ros_sensor_msgs::PointCloud2& msg = msg_with_echo.pointcloud;
    ROS_HEADER_SEQ(export_msg.header, msg.header.seq); // export_msg.header.seq = msg.header.seq;
    export_msg.header.timestamp_sec = sec(msg.header.stamp); // msg.header.stamp.sec;
    export_msg.header.timestamp_nsec = nsec(msg.header.stamp); // msg.header.stamp.nsec;
    strncpy(export_msg.header.frame_id, msg.header.frame_id.c_str(), sizeof(export_msg.header.frame_id) - 2);
    export_msg.width = msg.width;
    export_msg.height = msg.height;
    export_msg.is_bigendian = msg.is_bigendian;
    export_msg.is_dense = msg.is_dense;
    export_msg.point_step = msg.point_step;
    export_msg.row_step = msg.row_step;
    export_msg.num_echos = msg_with_echo.num_echos;
    export_msg.segment_idx = msg_with_echo.segment_idx;
    // Copy field descriptions
    int num_fields = msg.fields.size();
    std::vector<SickScanPointFieldMsg> export_fields(num_fields);
    for(int n = 0; n < num_fields; n++)
    {
        SickScanPointFieldMsg export_field;
        memset(&export_field, 0, sizeof(export_field));
        strncpy(export_field.name, msg.fields[n].name.c_str(), sizeof(export_field.name) - 2);
        export_field.offset = msg.fields[n].offset;
        export_field.datatype = msg.fields[n].datatype;
        export_field.count = msg.fields[n].count;
        export_fields[n] = export_field;
    }
    export_msg.fields.buffer = (SickScanPointFieldMsg*)malloc(num_fields * sizeof(SickScanPointFieldMsg));
    if (export_msg.fields.buffer != 0)
    {
        export_msg.fields.size = num_fields;
        export_msg.fields.capacity = num_fields;
        memcpy(export_msg.fields.buffer, export_fields.data(), num_fields * sizeof(SickScanPointFieldMsg));
    }
    // Copy pointcloud data
    export_msg.data.buffer = (uint8_t*)malloc(msg.row_step * msg.height);
    if (export_msg.data.buffer != 0)
    {
        export_msg.data.size = msg.row_step * msg.height;
        export_msg.data.capacity = msg.row_step * msg.height;
        memcpy(export_msg.data.buffer, msg.data.data(), msg.row_step * msg.height);
    }
    // Return converted pointcloud
    return export_msg;
}

static void freePointCloudMsg(SickScanPointCloudMsg& export_msg)
{
    if (export_msg.fields.buffer != 0)
        free(export_msg.fields.buffer);
    if (export_msg.data.buffer != 0)
        free(export_msg.data.buffer);
    memset(&export_msg, 0, sizeof(export_msg));
}

static SickScanImuMsg convertImuMsg(const ros_sensor_msgs::Imu& src_msg)
{
    SickScanImuMsg dst_msg;
    memset(&dst_msg, 0, sizeof(dst_msg));
    // Copy header
    ROS_HEADER_SEQ(dst_msg.header, src_msg.header.seq);
    dst_msg.header.timestamp_sec = sec(src_msg.header.stamp);
    dst_msg.header.timestamp_nsec = nsec(src_msg.header.stamp);
    strncpy(dst_msg.header.frame_id, src_msg.header.frame_id.c_str(), sizeof(dst_msg.header.frame_id) - 2);
    // Copy imu data
    dst_msg.orientation.x = src_msg.orientation.x;
    dst_msg.orientation.y = src_msg.orientation.y;
    dst_msg.orientation.z = src_msg.orientation.z;
    dst_msg.orientation.w = src_msg.orientation.w;
    for(int n = 0; n < 9; n++)
        dst_msg.orientation_covariance[n] = src_msg.orientation_covariance[n];
    dst_msg.angular_velocity.x = src_msg.angular_velocity.x;
    dst_msg.angular_velocity.y = src_msg.angular_velocity.y;
    dst_msg.angular_velocity.z = src_msg.angular_velocity.z;
    for(int n = 0; n < 9; n++)
        dst_msg.angular_velocity_covariance[n] = src_msg.angular_velocity_covariance[n];
    dst_msg.linear_acceleration.x = src_msg.linear_acceleration.x;
    dst_msg.linear_acceleration.y = src_msg.linear_acceleration.y;
    dst_msg.linear_acceleration.z = src_msg.linear_acceleration.z;
    for(int n = 0; n < 9; n++)
        dst_msg.linear_acceleration_covariance[n] = src_msg.linear_acceleration_covariance[n];
    return dst_msg;
}

static void freeImuMsg(SickScanImuMsg& msg)
{
    memset(&msg, 0, sizeof(msg));
}

static SickScanLFErecMsg convertLFErecMsg(const sick_scan_msg::LFErecMsg& src_msg)
{
    SickScanLFErecMsg dst_msg;
    memset(&dst_msg, 0, sizeof(dst_msg));
    // Copy header
    ROS_HEADER_SEQ(dst_msg.header, src_msg.header.seq);
    dst_msg.header.timestamp_sec = sec(src_msg.header.stamp);
    dst_msg.header.timestamp_nsec = nsec(src_msg.header.stamp);
    strncpy(dst_msg.header.frame_id, src_msg.header.frame_id.c_str(), sizeof(dst_msg.header.frame_id) - 2);
    // Copy LFErec data
    int max_fields_number = (int)(sizeof(dst_msg.fields) / sizeof(dst_msg.fields[0]));
    dst_msg.fields_number = ((src_msg.fields_number < max_fields_number) ? src_msg.fields_number : max_fields_number);
    for(int n = 0; n < dst_msg.fields_number; n++)
    {
        dst_msg.fields[n].version_number = src_msg.fields[n].version_number;
        dst_msg.fields[n].field_index = src_msg.fields[n].field_index;
        dst_msg.fields[n].sys_count = src_msg.fields[n].sys_count;
        dst_msg.fields[n].dist_scale_factor = src_msg.fields[n].dist_scale_factor;
        dst_msg.fields[n].dist_scale_offset = src_msg.fields[n].dist_scale_offset;
        dst_msg.fields[n].angle_scale_factor = src_msg.fields[n].angle_scale_factor;
        dst_msg.fields[n].angle_scale_offset = src_msg.fields[n].angle_scale_offset;
        dst_msg.fields[n].field_result_mrs = src_msg.fields[n].field_result_mrs;
        dst_msg.fields[n].time_state = src_msg.fields[n].time_state;
        dst_msg.fields[n].year = src_msg.fields[n].year;
        dst_msg.fields[n].month = src_msg.fields[n].month;
        dst_msg.fields[n].day = src_msg.fields[n].day;
        dst_msg.fields[n].hour = src_msg.fields[n].hour;
        dst_msg.fields[n].minute = src_msg.fields[n].minute;
        dst_msg.fields[n].second = src_msg.fields[n].second;
        dst_msg.fields[n].microsecond = src_msg.fields[n].microsecond;
    }
    return dst_msg;
}

static void freeLFErecMsg(SickScanLFErecMsg& msg)
{
    memset(&msg, 0, sizeof(msg));
}

static SickScanLIDoutputstateMsg convertLIDoutputstateMsg(const sick_scan_msg::LIDoutputstateMsg& src_msg)
{
    SickScanLIDoutputstateMsg dst_msg;
    memset(&dst_msg, 0, sizeof(dst_msg));
    // Copy header
    ROS_HEADER_SEQ(dst_msg.header, src_msg.header.seq);
    dst_msg.header.timestamp_sec = sec(src_msg.header.stamp);
    dst_msg.header.timestamp_nsec = nsec(src_msg.header.stamp);
    strncpy(dst_msg.header.frame_id, src_msg.header.frame_id.c_str(), sizeof(dst_msg.header.frame_id) - 2);
    // Copy LIDoutputstate data
    dst_msg.version_number = src_msg.version_number;
    dst_msg.system_counter = src_msg.system_counter;
    int max_states = (int)(sizeof(dst_msg.output_state) / sizeof(dst_msg.output_state[0]));
    int max_counts = (int)(sizeof(dst_msg.output_count) / sizeof(dst_msg.output_count[0]));
    for(int n = 0; n < src_msg.output_state.size() && n < max_states; n++)
        dst_msg.output_state[n] = src_msg.output_state[n];
    for(int n = 0; n < src_msg.output_count.size() && n < max_counts; n++)
        dst_msg.output_count[n] = src_msg.output_count[n];
    dst_msg.time_state = src_msg.time_state;
    dst_msg.year = src_msg.year;
    dst_msg.month = src_msg.month;
    dst_msg.day = src_msg.day;
    dst_msg.hour = src_msg.hour;
    dst_msg.minute = src_msg.minute;
    dst_msg.second = src_msg.second;
    dst_msg.microsecond = src_msg.microsecond;
    return dst_msg;
}

static void freeLIDoutputstateMsg(SickScanLIDoutputstateMsg& msg)
{
    memset(&msg, 0, sizeof(msg));
}

static SickScanRadarScan convertRadarScanMsg(const sick_scan_msg::RadarScan& src_msg)
{
    SickScanRadarScan dst_msg;
    memset(&dst_msg, 0, sizeof(dst_msg));
    // Copy header
    ROS_HEADER_SEQ(dst_msg.header, src_msg.header.seq);
    dst_msg.header.timestamp_sec = sec(src_msg.header.stamp);
    dst_msg.header.timestamp_nsec = nsec(src_msg.header.stamp);
    strncpy(dst_msg.header.frame_id, src_msg.header.frame_id.c_str(), sizeof(dst_msg.header.frame_id) - 2);
    // Copy radarpreheader data
    dst_msg.radarpreheader.uiversionno = src_msg.radarpreheader.uiversionno;
    dst_msg.radarpreheader.uiident = src_msg.radarpreheader.radarpreheaderdeviceblock.uiident;
    dst_msg.radarpreheader.udiserialno = src_msg.radarpreheader.radarpreheaderdeviceblock.udiserialno;
    dst_msg.radarpreheader.bdeviceerror = src_msg.radarpreheader.radarpreheaderdeviceblock.bdeviceerror;
    dst_msg.radarpreheader.bcontaminationwarning = src_msg.radarpreheader.radarpreheaderdeviceblock.bcontaminationwarning;
    dst_msg.radarpreheader.bcontaminationerror = src_msg.radarpreheader.radarpreheaderdeviceblock.bcontaminationerror;
    dst_msg.radarpreheader.uitelegramcount = src_msg.radarpreheader.radarpreheaderstatusblock.uitelegramcount;
    dst_msg.radarpreheader.uicyclecount = src_msg.radarpreheader.radarpreheaderstatusblock.uicyclecount;
    dst_msg.radarpreheader.udisystemcountscan = src_msg.radarpreheader.radarpreheaderstatusblock.udisystemcountscan;
    dst_msg.radarpreheader.udisystemcounttransmit = src_msg.radarpreheader.radarpreheaderstatusblock.udisystemcounttransmit;
    dst_msg.radarpreheader.uiinputs = src_msg.radarpreheader.radarpreheaderstatusblock.uiinputs;
    dst_msg.radarpreheader.uioutputs = src_msg.radarpreheader.radarpreheaderstatusblock.uioutputs;
    dst_msg.radarpreheader.uicycleduration = src_msg.radarpreheader.radarpreheadermeasurementparam1block.uicycleduration;
    dst_msg.radarpreheader.uinoiselevel = src_msg.radarpreheader.radarpreheadermeasurementparam1block.uinoiselevel;
    dst_msg.radarpreheader.numencoder = src_msg.radarpreheader.radarpreheaderarrayencoderblock.size();
    int max_encpositions = (int)(sizeof(dst_msg.radarpreheader.udiencoderpos) / sizeof(dst_msg.radarpreheader.udiencoderpos[0]));
    int max_encspeedvals = (int)(sizeof(dst_msg.radarpreheader.iencoderspeed) / sizeof(dst_msg.radarpreheader.iencoderspeed[0]));
    dst_msg.radarpreheader.numencoder = ((dst_msg.radarpreheader.numencoder < max_encpositions) ? dst_msg.radarpreheader.numencoder : max_encpositions);
    dst_msg.radarpreheader.numencoder = ((dst_msg.radarpreheader.numencoder < max_encspeedvals) ? dst_msg.radarpreheader.numencoder : max_encspeedvals);
    for(int n = 0; n < dst_msg.radarpreheader.numencoder; n++)
    {
        dst_msg.radarpreheader.udiencoderpos[n] = src_msg.radarpreheader.radarpreheaderarrayencoderblock[n].udiencoderpos;
        dst_msg.radarpreheader.iencoderspeed[n] = src_msg.radarpreheader.radarpreheaderarrayencoderblock[n].iencoderspeed;
    }
    // Copy radar target pointcloud data
    sick_scan::PointCloud2withEcho targets_with_echo(&src_msg.targets, 1, 0);
    dst_msg.targets = convertPointCloudMsg(targets_with_echo);
    // Copy radar object data
    dst_msg.objects.size = src_msg.objects.size();
    dst_msg.objects.capacity = dst_msg.objects.size;
    dst_msg.objects.buffer = (SickScanRadarObject*)malloc(dst_msg.objects.capacity * sizeof(SickScanRadarObject));
    if (!dst_msg.objects.buffer)
    {
        dst_msg.objects.size = 0;
        dst_msg.objects.capacity = 0;
    }
    for(int n = 0; n < dst_msg.objects.size; n++)
    {
        dst_msg.objects.buffer[n].id = src_msg.objects[n].id;
        dst_msg.objects.buffer[n].tracking_time_sec = sec(src_msg.objects[n].tracking_time);
        dst_msg.objects.buffer[n].tracking_time_nsec = nsec(src_msg.objects[n].tracking_time);
        dst_msg.objects.buffer[n].last_seen_sec = sec(src_msg.objects[n].last_seen);
        dst_msg.objects.buffer[n].last_seen_nsec = nsec(src_msg.objects[n].last_seen);
        dst_msg.objects.buffer[n].velocity_linear.x = src_msg.objects[n].velocity.twist.linear.x;
        dst_msg.objects.buffer[n].velocity_linear.y = src_msg.objects[n].velocity.twist.linear.y;
        dst_msg.objects.buffer[n].velocity_linear.z = src_msg.objects[n].velocity.twist.linear.y;
        dst_msg.objects.buffer[n].velocity_angular.x = src_msg.objects[n].velocity.twist.angular.x;
        dst_msg.objects.buffer[n].velocity_angular.y = src_msg.objects[n].velocity.twist.angular.y;
        dst_msg.objects.buffer[n].velocity_angular.z = src_msg.objects[n].velocity.twist.angular.y;
        for(int m = 0; m < 36; m++)
            dst_msg.objects.buffer[n].velocity_covariance[m] = src_msg.objects[n].velocity.covariance[m];
        dst_msg.objects.buffer[n].bounding_box_center_position.x = src_msg.objects[n].bounding_box_center.position.x;
        dst_msg.objects.buffer[n].bounding_box_center_position.y = src_msg.objects[n].bounding_box_center.position.y;
        dst_msg.objects.buffer[n].bounding_box_center_position.z = src_msg.objects[n].bounding_box_center.position.z;
        dst_msg.objects.buffer[n].bounding_box_center_orientation.x = src_msg.objects[n].bounding_box_center.orientation.x;
        dst_msg.objects.buffer[n].bounding_box_center_orientation.y = src_msg.objects[n].bounding_box_center.orientation.y;
        dst_msg.objects.buffer[n].bounding_box_center_orientation.z = src_msg.objects[n].bounding_box_center.orientation.z;
        dst_msg.objects.buffer[n].bounding_box_center_orientation.w = src_msg.objects[n].bounding_box_center.orientation.w;
        dst_msg.objects.buffer[n].bounding_box_size.x = src_msg.objects[n].bounding_box_size.x;
        dst_msg.objects.buffer[n].bounding_box_size.y = src_msg.objects[n].bounding_box_size.y;
        dst_msg.objects.buffer[n].bounding_box_size.z = src_msg.objects[n].bounding_box_size.z;
        dst_msg.objects.buffer[n].object_box_center_position.x = src_msg.objects[n].object_box_center.pose.position.x;
        dst_msg.objects.buffer[n].object_box_center_position.y = src_msg.objects[n].object_box_center.pose.position.y;
        dst_msg.objects.buffer[n].object_box_center_position.z = src_msg.objects[n].object_box_center.pose.position.z;
        dst_msg.objects.buffer[n].object_box_center_orientation.x = src_msg.objects[n].object_box_center.pose.orientation.x;
        dst_msg.objects.buffer[n].object_box_center_orientation.y = src_msg.objects[n].object_box_center.pose.orientation.y;
        dst_msg.objects.buffer[n].object_box_center_orientation.z = src_msg.objects[n].object_box_center.pose.orientation.z;
        dst_msg.objects.buffer[n].object_box_center_orientation.w = src_msg.objects[n].object_box_center.pose.orientation.w;
        for(int m = 0; m < 36; m++)
            dst_msg.objects.buffer[n].object_box_center_covariance[m] = src_msg.objects[n].object_box_center.covariance[m];
        dst_msg.objects.buffer[n].object_box_size.x = src_msg.objects[n].object_box_size.x;
        dst_msg.objects.buffer[n].object_box_size.y = src_msg.objects[n].object_box_size.y;
        dst_msg.objects.buffer[n].object_box_size.z = src_msg.objects[n].object_box_size.z;
        dst_msg.objects.buffer[n].contour_points.size = src_msg.objects[n].contour_points.size();
        dst_msg.objects.buffer[n].contour_points.capacity = dst_msg.objects.buffer[n].contour_points.size;
        dst_msg.objects.buffer[n].contour_points.buffer = (SickScanVector3Msg*)malloc(dst_msg.objects.buffer[n].contour_points.capacity * sizeof(SickScanVector3Msg));
        if (!dst_msg.objects.buffer[n].contour_points.buffer)
        {
            dst_msg.objects.buffer[n].contour_points.size = 0;
            dst_msg.objects.buffer[n].contour_points.capacity = 0;
        }
        for(int m = 0; m < dst_msg.objects.buffer[n].contour_points.size; m++)
        {
            dst_msg.objects.buffer[n].contour_points.buffer[m].x = src_msg.objects[n].contour_points[m].x;
            dst_msg.objects.buffer[n].contour_points.buffer[m].y = src_msg.objects[n].contour_points[m].y;
            dst_msg.objects.buffer[n].contour_points.buffer[m].z = src_msg.objects[n].contour_points[m].z;

        }
    }
    return dst_msg;
}

static void freeRadarScanMsg(SickScanRadarScan& msg)
{
    freePointCloudMsg(msg.targets);
    for(int n = 0; n < msg.objects.size; n++)
        free(msg.objects.buffer[n].contour_points.buffer);
    free(msg.objects.buffer);
    memset(&msg, 0, sizeof(msg));
}

static SickScanLdmrsObjectArray convertLdmrsObjectArrayMsg(const sick_scan_msg::SickLdmrsObjectArray& src_msg)
{
    SickScanLdmrsObjectArray dst_msg;
    memset(&dst_msg, 0, sizeof(dst_msg));
    // Copy header
    ROS_HEADER_SEQ(dst_msg.header, src_msg.header.seq);
    dst_msg.header.timestamp_sec = sec(src_msg.header.stamp);
    dst_msg.header.timestamp_nsec = nsec(src_msg.header.stamp);
    strncpy(dst_msg.header.frame_id, src_msg.header.frame_id.c_str(), sizeof(dst_msg.header.frame_id) - 2);
    // Copy ldmrs objects
    dst_msg.objects.size = src_msg.objects.size();
    dst_msg.objects.capacity = dst_msg.objects.size;
    dst_msg.objects.buffer = (SickScanLdmrsObject*)malloc(dst_msg.objects.capacity * sizeof(SickScanLdmrsObject));
    if (!dst_msg.objects.buffer)
    {
        dst_msg.objects.size = 0;
        dst_msg.objects.capacity = 0;
    }
    for(int n = 0; n < dst_msg.objects.size; n++)
    {
        dst_msg.objects.buffer[n].id = src_msg.objects[n].id;
        dst_msg.objects.buffer[n].tracking_time_sec = sec(src_msg.objects[n].tracking_time);
        dst_msg.objects.buffer[n].tracking_time_nsec = nsec(src_msg.objects[n].tracking_time);
        dst_msg.objects.buffer[n].last_seen_sec = sec(src_msg.objects[n].last_seen);
        dst_msg.objects.buffer[n].last_seen_nsec = nsec(src_msg.objects[n].last_seen);
        dst_msg.objects.buffer[n].velocity_linear.x = src_msg.objects[n].velocity.twist.linear.x;
        dst_msg.objects.buffer[n].velocity_linear.y = src_msg.objects[n].velocity.twist.linear.y;
        dst_msg.objects.buffer[n].velocity_linear.z = src_msg.objects[n].velocity.twist.linear.y;
        dst_msg.objects.buffer[n].velocity_angular.x = src_msg.objects[n].velocity.twist.angular.x;
        dst_msg.objects.buffer[n].velocity_angular.y = src_msg.objects[n].velocity.twist.angular.y;
        dst_msg.objects.buffer[n].velocity_angular.z = src_msg.objects[n].velocity.twist.angular.y;
        for(int m = 0; m < 36; m++)
            dst_msg.objects.buffer[n].velocity_covariance[m] = src_msg.objects[n].velocity.covariance[m];
        dst_msg.objects.buffer[n].bounding_box_center_position.x = src_msg.objects[n].bounding_box_center.position.x;
        dst_msg.objects.buffer[n].bounding_box_center_position.y = src_msg.objects[n].bounding_box_center.position.y;
        dst_msg.objects.buffer[n].bounding_box_center_position.z = src_msg.objects[n].bounding_box_center.position.z;
        dst_msg.objects.buffer[n].bounding_box_center_orientation.x = src_msg.objects[n].bounding_box_center.orientation.x;
        dst_msg.objects.buffer[n].bounding_box_center_orientation.y = src_msg.objects[n].bounding_box_center.orientation.y;
        dst_msg.objects.buffer[n].bounding_box_center_orientation.z = src_msg.objects[n].bounding_box_center.orientation.z;
        dst_msg.objects.buffer[n].bounding_box_center_orientation.w = src_msg.objects[n].bounding_box_center.orientation.w;
        dst_msg.objects.buffer[n].bounding_box_size.x = src_msg.objects[n].bounding_box_size.x;
        dst_msg.objects.buffer[n].bounding_box_size.y = src_msg.objects[n].bounding_box_size.y;
        dst_msg.objects.buffer[n].bounding_box_size.z = src_msg.objects[n].bounding_box_size.z;
        dst_msg.objects.buffer[n].object_box_center_position.x = src_msg.objects[n].object_box_center.pose.position.x;
        dst_msg.objects.buffer[n].object_box_center_position.y = src_msg.objects[n].object_box_center.pose.position.y;
        dst_msg.objects.buffer[n].object_box_center_position.z = src_msg.objects[n].object_box_center.pose.position.z;
        dst_msg.objects.buffer[n].object_box_center_orientation.x = src_msg.objects[n].object_box_center.pose.orientation.x;
        dst_msg.objects.buffer[n].object_box_center_orientation.y = src_msg.objects[n].object_box_center.pose.orientation.y;
        dst_msg.objects.buffer[n].object_box_center_orientation.z = src_msg.objects[n].object_box_center.pose.orientation.z;
        dst_msg.objects.buffer[n].object_box_center_orientation.w = src_msg.objects[n].object_box_center.pose.orientation.w;
        for(int m = 0; m < 36; m++)
            dst_msg.objects.buffer[n].object_box_center_covariance[m] = src_msg.objects[n].object_box_center.covariance[m];
        dst_msg.objects.buffer[n].object_box_size.x = src_msg.objects[n].object_box_size.x;
        dst_msg.objects.buffer[n].object_box_size.y = src_msg.objects[n].object_box_size.y;
        dst_msg.objects.buffer[n].object_box_size.z = src_msg.objects[n].object_box_size.z;
        dst_msg.objects.buffer[n].contour_points.size = src_msg.objects[n].contour_points.size();
        dst_msg.objects.buffer[n].contour_points.capacity = dst_msg.objects.buffer[n].contour_points.size;
        dst_msg.objects.buffer[n].contour_points.buffer = (SickScanVector3Msg*)malloc(dst_msg.objects.buffer[n].contour_points.capacity * sizeof(SickScanVector3Msg));
        if (!dst_msg.objects.buffer[n].contour_points.buffer)
        {
            dst_msg.objects.buffer[n].contour_points.size = 0;
            dst_msg.objects.buffer[n].contour_points.capacity = 0;
        }
        for(int m = 0; m < dst_msg.objects.buffer[n].contour_points.size; m++)
        {
            dst_msg.objects.buffer[n].contour_points.buffer[m].x = src_msg.objects[n].contour_points[m].x;
            dst_msg.objects.buffer[n].contour_points.buffer[m].y = src_msg.objects[n].contour_points[m].y;
            dst_msg.objects.buffer[n].contour_points.buffer[m].z = src_msg.objects[n].contour_points[m].z;

        }
    }
    return dst_msg;
}

static void freeLdmrsObjectArrayMsg(SickScanLdmrsObjectArray& msg)
{
    for(int n = 0; n < msg.objects.size; n++)
        free(msg.objects.buffer[n].contour_points.buffer);
    free(msg.objects.buffer);
    memset(&msg, 0, sizeof(msg));
}

static SickScanVisualizationMarkerMsg convertVisualizationMarkerMsg(const ros_visualization_msgs::MarkerArray& src_msg)
{
    SickScanVisualizationMarkerMsg dst_msg;
    memset(&dst_msg, 0, sizeof(dst_msg));
    if (src_msg.markers.size() > 0)
    {
        // Copy markers
        dst_msg.markers.size = src_msg.markers.size();
        dst_msg.markers.capacity = dst_msg.markers.size;
        dst_msg.markers.buffer = (SickScanVisualizationMarker*)malloc(dst_msg.markers.capacity * sizeof(SickScanVisualizationMarker));
        if (!dst_msg.markers.buffer)
        {
            dst_msg.markers.size = 0;
            dst_msg.markers.capacity = 0;
        }
        for(int n = 0; n < dst_msg.markers.size; n++)
        {
            const ros_visualization_msgs::Marker& src_marker = src_msg.markers[n];
            SickScanVisualizationMarker& dst_marker = dst_msg.markers.buffer[n];
            memset(&dst_marker, 0, sizeof(dst_marker));
            // Copy header
            ROS_HEADER_SEQ(dst_marker.header, src_marker.header.seq);
            dst_marker.header.timestamp_sec = sec(src_marker.header.stamp);
            dst_marker.header.timestamp_nsec = nsec(src_marker.header.stamp);
            strncpy(dst_marker.header.frame_id, src_marker.header.frame_id.c_str(), sizeof(dst_marker.header.frame_id) - 2);
            // Copy data
            strncpy(dst_marker.ns, src_marker.ns.c_str(), sizeof(dst_marker.ns) - 2);
            dst_marker.id = src_marker.id;
            dst_marker.type = src_marker.type;
            dst_marker.action = src_marker.action;
            dst_marker.pose_position.x = src_marker.pose.position.x;
            dst_marker.pose_position.y = src_marker.pose.position.y;
            dst_marker.pose_position.z = src_marker.pose.position.z;
            dst_marker.pose_orientation.x = src_marker.pose.orientation.x;
            dst_marker.pose_orientation.y = src_marker.pose.orientation.y;
            dst_marker.pose_orientation.z = src_marker.pose.orientation.z;
            dst_marker.pose_orientation.w = src_marker.pose.orientation.w;
            dst_marker.scale.x = src_marker.scale.x;
            dst_marker.scale.y = src_marker.scale.y;
            dst_marker.scale.z = src_marker.scale.z;
            dst_marker.color.r = src_marker.color.r;
            dst_marker.color.g = src_marker.color.g;
            dst_marker.color.b = src_marker.color.b;
            dst_marker.color.a = src_marker.color.a;
            dst_marker.lifetime_sec = sec(src_marker.lifetime);
            dst_marker.lifetime_nsec = nsec(src_marker.lifetime);
            dst_marker.frame_locked = src_marker.frame_locked;
            strncpy(dst_marker.text, src_marker.text.c_str(), sizeof(dst_marker.text) - 2);
            strncpy(dst_marker.mesh_resource, src_marker.mesh_resource.c_str(), sizeof(dst_marker.mesh_resource) - 2);
            dst_marker.mesh_use_embedded_materials = src_marker.mesh_use_embedded_materials;
            dst_marker.points.size = src_marker.points.size();
            dst_marker.points.capacity = dst_marker.points.size;
            dst_marker.points.buffer = (SickScanVector3Msg*)malloc(dst_marker.points.capacity * sizeof(SickScanVector3Msg));
            if (!dst_marker.points.buffer)
            {
                dst_marker.points.size = 0;
                dst_marker.points.capacity = 0;
            }
            for(int m = 0; m < dst_marker.points.size; m++)
            {
                dst_marker.points.buffer[m].x = src_marker.points[m].x;
                dst_marker.points.buffer[m].y = src_marker.points[m].y;
                dst_marker.points.buffer[m].z = src_marker.points[m].z;
            }
            dst_marker.colors.size = src_marker.colors.size();
            dst_marker.colors.capacity = dst_marker.colors.size;
            dst_marker.colors.buffer = (SickScanColorRGBA*)malloc(dst_marker.colors.capacity * sizeof(SickScanColorRGBA));
            if (!dst_marker.colors.buffer)
            {
                dst_marker.colors.size = 0;
                dst_marker.colors.capacity = 0;
            }
            for(int m = 0; m < dst_marker.colors.size; m++)
            {
                dst_marker.colors.buffer[m].r = src_marker.colors[m].r;
                dst_marker.colors.buffer[m].g = src_marker.colors[m].g;
                dst_marker.colors.buffer[m].b = src_marker.colors[m].b;
                dst_marker.colors.buffer[m].a = src_marker.colors[m].a;
            }
        }
    }
    return dst_msg;
}

static void freeVisualizationMarkerMsg(SickScanVisualizationMarkerMsg& msg)
{
    for(int n = 0; n < msg.markers.size; n++)
    {
        free(msg.markers.buffer[n].points.buffer);
        free(msg.markers.buffer[n].colors.buffer);
    }
    free(msg.markers.buffer);
    memset(&msg, 0, sizeof(msg));
}

/*
*  Callback handler
*/

static void cartesian_pointcloud_callback(rosNodePtr node, const sick_scan::PointCloud2withEcho* msg)
{
    ROS_DEBUG_STREAM("api_impl cartesian_pointcloud_callback: PointCloud2 message, " << msg->pointcloud.width << "x" << msg->pointcloud.height << " points");
    DUMP_API_POINTCLOUD_MESSAGE("impl", msg->pointcloud);
    // Convert ros_sensor_msgs::PointCloud2 message to SickScanPointCloudMsg and export (i.e. notify all listeners)
    SickScanPointCloudMsg export_msg = convertPointCloudMsg(*msg);
    SickScanApiHandle apiHandle = castNodeToApiHandle(node);
    s_callback_handler_cartesian_pointcloud_messages.notifyListener(apiHandle, &export_msg);
    freePointCloudMsg(export_msg);
}

static void polar_pointcloud_callback(rosNodePtr node, const sick_scan::PointCloud2withEcho* msg)
{
    ROS_DEBUG_STREAM("api_impl polar_pointcloud_callback: PointCloud2 message, " << msg->pointcloud.width << "x" << msg->pointcloud.height << " points");
    // Convert ros_sensor_msgs::PointCloud2 message to SickScanPointCloudMsg and export (i.e. notify all listeners)
    SickScanPointCloudMsg export_msg = convertPointCloudMsg(*msg);
    SickScanApiHandle apiHandle = castNodeToApiHandle(node);
    s_callback_handler_polar_pointcloud_messages.notifyListener(apiHandle, &export_msg);
    freePointCloudMsg(export_msg);
}

static void imu_callback(rosNodePtr node, const ros_sensor_msgs::Imu* msg)
{
    // ROS_DEBUG_STREAM("api_impl lferec_callback: Imu message = {" << (*msg) << "}");
    DUMP_API_IMU_MESSAGE("impl", *msg);
    ROS_DEBUG_STREAM("api_impl imu_callback: Imu message, orientation={" << msg->orientation << "}, angular_velocity={" << msg->angular_velocity << "}, linear_acceleration={" << msg->linear_acceleration << "}");
    // Convert ros_sensor_msgs::PointCloud2 message to SickScanPointCloudMsg and export (i.e. notify all listeners)
    SickScanImuMsg export_msg = convertImuMsg(*msg);
    SickScanApiHandle apiHandle = castNodeToApiHandle(node);
    s_callback_handler_imu_messages.notifyListener(apiHandle, &export_msg);
    freeImuMsg(export_msg);
}

static void lferec_callback(rosNodePtr node, const sick_scan_msg::LFErecMsg* msg)
{
    // ROS_DEBUG_STREAM("api_impl lferec_callback: LFErec message = {" << (*msg) << "}");
    DUMP_API_LFEREC_MESSAGE("impl", *msg);
    std::stringstream field_info;
    for(int n = 0; n  < msg->fields_number; n++)
    {
        field_info << ", field " << (int)(msg->fields[n].field_index) << ": (";
        if (msg->fields[n].field_result_mrs == 1)
            field_info << "free,";
        else if (msg->fields[n].field_result_mrs == 2)
            field_info << "infringed,";
        else
            field_info << "invalid,";
        field_info << msg->fields[n].dist_scale_factor << "," << msg->fields[n].dist_scale_offset << "," << msg->fields[n].angle_scale_factor << "," << msg->fields[n].angle_scale_offset << ")";
    }
    ROS_DEBUG_STREAM("api_impl lferec_callback: LFErec message, " << msg->fields_number << " fields" << field_info.str());
    SickScanLFErecMsg export_msg = convertLFErecMsg(*msg);
    SickScanApiHandle apiHandle = castNodeToApiHandle(node);
    s_callback_handler_lferec_messages.notifyListener(apiHandle, &export_msg);
    freeLFErecMsg(export_msg);
}

static void lidoutputstate_callback(rosNodePtr node, const sick_scan_msg::LIDoutputstateMsg* msg)
{
    // ROS_DEBUG_STREAM("api_impl lidoutputstate_callback: LIDoutputstate message = {" << (*msg) << "}");
    DUMP_API_LIDOUTPUTSTATE_MESSAGE("impl", *msg);
    std::stringstream state_info;
    state_info << ", outputstate=(";
    for(int n = 0; n  < msg->output_state.size(); n++)
        state_info << (n > 0 ? ",": "") << (int)(msg->output_state[n]);
    state_info << "), outputcount=(";
    for(int n = 0; n  < msg->output_count.size(); n++)
        state_info << (n > 0 ? ",": "") << (int)(msg->output_count[n]);
    state_info << ")";
    ROS_DEBUG_STREAM("api_impl lidoutputstate_callback: LIDoutputstate message" << state_info.str());
    SickScanLIDoutputstateMsg export_msg = convertLIDoutputstateMsg(*msg);
    SickScanApiHandle apiHandle = castNodeToApiHandle(node);
    s_callback_handler_lidoutputstate_messages.notifyListener(apiHandle, &export_msg);
    freeLIDoutputstateMsg(export_msg);
}

static void radarscan_callback(rosNodePtr node, const sick_scan_msg::RadarScan* msg)
{
    // ROS_DEBUG_STREAM("api_impl radarscan_callback: RadarScan message = {" << (*msg) << "}");
    DUMP_API_RADARSCAN_MESSAGE("impl", *msg);
    ROS_DEBUG_STREAM("api_impl radarscan_callback: " << (msg->targets.width * msg->targets.height) << " targets, "  << msg->objects.size() << " objects");
    SickScanRadarScan export_msg = convertRadarScanMsg(*msg);
    SickScanApiHandle apiHandle = castNodeToApiHandle(node);
    s_callback_handler_radarscan_messages.notifyListener(apiHandle, &export_msg);
    freeRadarScanMsg(export_msg);
}

static void ldmrsobjectarray_callback(rosNodePtr node, const sick_scan_msg::SickLdmrsObjectArray* msg)
{
    // ROS_DEBUG_STREAM("api_impl ldmrsobjectarray_callback: LdmrsObjectArray message = {" << (*msg) << "}");
    DUMP_API_LDMRSOBJECTARRAY_MESSAGE("impl", *msg);
    ROS_DEBUG_STREAM("api_impl ldmrsobjectarray_callback: " << msg->objects.size() << " objects");
    SickScanLdmrsObjectArray export_msg = convertLdmrsObjectArrayMsg(*msg);
    SickScanApiHandle apiHandle = castNodeToApiHandle(node);
    s_callback_handler_ldmrsobjectarray_messages.notifyListener(apiHandle, &export_msg);
    freeLdmrsObjectArrayMsg(export_msg);
}

static void visualizationmarker_callback(rosNodePtr node, const ros_visualization_msgs::MarkerArray* msg)
{
    // ROS_DEBUG_STREAM("api_impl visualizationmarker_callback: MarkerArray message = {" << (*msg) << "}");
    DUMP_API_VISUALIZATIONMARKER_MESSAGE("impl", *msg);
    std::stringstream marker_info;
    for(int n = 0; n < msg->markers.size(); n++)
        marker_info << ", marker " << msg->markers[n].id << ": pos=(" << msg->markers[n].pose.position.x << "," << msg->markers[n].pose.position.y << "," << msg->markers[n].pose.position.z << ")";
    ROS_DEBUG_STREAM("api_impl visualizationmarker_callback: " << msg->markers.size() << " markers" << marker_info.str());
    SickScanVisualizationMarkerMsg export_msg = convertVisualizationMarkerMsg(*msg);
    SickScanApiHandle apiHandle = castNodeToApiHandle(node);
    s_callback_handler_visualizationmarker_messages.notifyListener(apiHandle, &export_msg);
    freeVisualizationMarkerMsg(export_msg);
}

/*
*  Functions to initialize and close the API and a lidar
*/

/*
*  Create an instance of sick_scan_xd api.
*  Optional commandline arguments argc, argv identical to sick_generic_caller.
*  Call SickScanApiInitByLaunchfile or SickScanApiInitByCli to process a lidar.
*/
SickScanApiHandle SickScanApiCreate(int argc, char** argv)
{
    try
    {
        std::string versionInfo = std::string("sick_scan_api V. ") + getVersionInfo();
        setVersionInfo(versionInfo);

        #if defined __ROS_VERSION && __ROS_VERSION == 2
        rclcpp::init(argc, argv);
        rclcpp::NodeOptions node_options;
        node_options.allow_undeclared_parameters(true);
        rosNodePtr node = rclcpp::Node::make_shared("sick_scan", "", node_options);
        SickScanApiHandle apiHandle = castNodeToApiHandle(node);
        #else
        ros::init(argc, argv, s_scannerName, ros::init_options::NoSigintHandler);
        SickScanApiHandle apiHandle = new ros::NodeHandle("~");
        #endif

        signal(SIGINT, rosSignalHandler);
        ROS_INFO_STREAM(versionInfo);

        if (argc > 0 && argv != 0 && argv[0] != 0)
            s_api_caller[apiHandle] = argv[0];
        return apiHandle;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiCreate(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiCreate(): unknown exception ");
    }
    return 0;
}

// Release and free all resources of a handle; the handle is invalid after SickScanApiRelease
int32_t SickScanApiRelease(SickScanApiHandle apiHandle)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRelease(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_api_caller[apiHandle].clear();
        s_callback_handler_cartesian_pointcloud_messages.clear();
        s_callback_handler_polar_pointcloud_messages.clear();
        s_callback_handler_imu_messages.clear();
        s_callback_handler_lferec_messages.clear();
        s_callback_handler_lidoutputstate_messages.clear();
        s_callback_handler_radarscan_messages.clear();
        s_callback_handler_ldmrsobjectarray_messages.clear();
        s_callback_handler_visualizationmarker_messages.clear();
        for(int n = 0; n < s_malloced_resources.size(); n++)
            free(s_malloced_resources[n]);
        s_malloced_resources.clear();
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRelease(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRelease(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Initializes a lidar by launchfile and starts message receiving and processing
int32_t SickScanApiInitByLaunchfile(SickScanApiHandle apiHandle, const char* launchfile_args)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiInitByLaunchfile(" << launchfile_args << "): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        // Split launchfile_args by spaces
        ROS_INFO_STREAM("SickScanApiInitByLaunchfile: launchfile_args = \"" << launchfile_args << "\"");
        std::istringstream args_stream(launchfile_args);
        std::string arg;
        std::vector<std::string> args;
        while (getline(args_stream, arg, ' ' ))
            args.push_back(arg);
        // Convert to argc, argv
        int argc = args.size() + 1;
        char** argv = (char**)malloc(argc * sizeof(char*));
        s_malloced_resources.push_back(argv);
        argv[0] = (char*)malloc(s_api_caller[apiHandle].size() + 1);
        strcpy(argv[0], s_api_caller[apiHandle].c_str());
        s_malloced_resources.push_back(argv[0]);
        for(int n = 1; n < argc; n++)
        {
            argv[n] = (char*)malloc(strlen(args[n-1].c_str()) + 1);
            strcpy(argv[n], args[n-1].c_str());
            s_malloced_resources.push_back(argv[n]);
        }
        // Init using SickScanApiInitByCli
        int32_t ret = SickScanApiInitByCli(apiHandle, argc, argv);
        return ret;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiInitByLaunchfile(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiInitByLaunchfile(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Initializes a lidar by commandline arguments and starts message receiving and processing
int32_t SickScanApiInitByCli(SickScanApiHandle apiHandle, int argc, char** argv)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiInitByCli(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        std::stringstream cli_params;
        for(int n = 0; n < argc; n++)
            cli_params << (n > 0 ? " ": "")  << argv[n];
        ROS_INFO_STREAM("SickScanApiInitByCli: " << cli_params.str());
        
        // Start sick_scan event loop
        int exit_code = 0;
        rosNodePtr node = castApiHandleToNode(apiHandle);
        if (!startGenericLaser(argc, argv, s_scannerName, node, &exit_code) || exit_code != sick_scan::ExitSuccess)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiInitByCli(): startGenericLaser() failed, could not start generic laser event loop");
            return SICK_SCAN_API_ERROR;
        }
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiInitByCli(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiInitByCli(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Stops message receiving and processing and closes a lidar
int32_t SickScanApiClose(SickScanApiHandle apiHandle)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiClose(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        stopScannerAndExit(true);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiClose(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiClose(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

/*
*  Registration / deregistration of message callbacks
*/

// Register / deregister a callback for cartesian PointCloud messages, pointcloud in cartesian coordinates with fields x, y, z, intensity
int32_t SickScanApiRegisterCartesianPointCloudMsg(SickScanApiHandle apiHandle, SickScanPointCloudMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRegisterCartesianPointCloudMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_cartesian_pointcloud_messages.addListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::addCartesianPointcloudListener(node, cartesian_pointcloud_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterCartesianPointCloudMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterCartesianPointCloudMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}
int32_t SickScanApiDeregisterCartesianPointCloudMsg(SickScanApiHandle apiHandle, SickScanPointCloudMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterCartesianPointCloudMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_cartesian_pointcloud_messages.removeListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::removeCartesianPointcloudListener(node, cartesian_pointcloud_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterCartesianPointCloudMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterCartesianPointCloudMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Register / deregister a callback for polar PointCloud messages, pointcloud in polar coordinates with fields range, azimuth, elevation, intensity
int32_t SickScanApiRegisterPolarPointCloudMsg(SickScanApiHandle apiHandle, SickScanPointCloudMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRegisterPolarPointCloudMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_polar_pointcloud_messages.addListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::addPolarPointcloudListener(node, polar_pointcloud_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterPolarPointCloudMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterPolarPointCloudMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}
int32_t SickScanApiDeregisterPolarPointCloudMsg(SickScanApiHandle apiHandle, SickScanPointCloudMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterPolarPointCloudMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_polar_pointcloud_messages.removeListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::removePolarPointcloudListener(node, polar_pointcloud_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterPolarPointCloudMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterPolarPointCloudMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Register / deregister a callback for Imu messages
int32_t SickScanApiRegisterImuMsg(SickScanApiHandle apiHandle, SickScanImuMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRegisterImuMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_imu_messages.addListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::addImuListener(node, imu_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterImuMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterImuMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}
int32_t SickScanApiDeregisterImuMsg(SickScanApiHandle apiHandle, SickScanImuMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterImuMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_imu_messages.removeListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::removeImuListener(node, imu_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterImuMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterImuMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Register / deregister a callback for SickScanLFErecMsg messages
int32_t SickScanApiRegisterLFErecMsg(SickScanApiHandle apiHandle, SickScanLFErecMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLFErecMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_lferec_messages.addListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::addLFErecListener(node, lferec_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLFErecMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLFErecMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}
int32_t SickScanApiDeregisterLFErecMsg(SickScanApiHandle apiHandle, SickScanLFErecMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLFErecMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_lferec_messages.removeListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::removeLFErecListener(node, lferec_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLFErecMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLFErecMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Register / deregister a callback for SickScanLIDoutputstateMsg messages
int32_t SickScanApiRegisterLIDoutputstateMsg(SickScanApiHandle apiHandle, SickScanLIDoutputstateMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLIDoutputstateMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_lidoutputstate_messages.addListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::addLIDoutputstateListener(node, lidoutputstate_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLIDoutputstateMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLIDoutputstateMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}
int32_t SickScanApiDeregisterLIDoutputstateMsg(SickScanApiHandle apiHandle, SickScanLIDoutputstateMsgCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLIDoutputstateMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_lidoutputstate_messages.removeListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::removeLIDoutputstateListener(node, lidoutputstate_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLIDoutputstateMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLIDoutputstateMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Register / deregister a callback for SickScanRadarScan messages
int32_t SickScanApiRegisterRadarScanMsg(SickScanApiHandle apiHandle, SickScanRadarScanCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRegisterRadarScanMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_radarscan_messages.addListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::addRadarScanListener(node, radarscan_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterRadarScanMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterRadarScanMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}
int32_t SickScanApiDeregisterRadarScanMsg(SickScanApiHandle apiHandle, SickScanRadarScanCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterRadarScanMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_radarscan_messages.removeListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::removeRadarScanListener(node, radarscan_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterRadarScanMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterRadarScanMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Register / deregister a callback for SickScanLdmrsObjectArray messages
int32_t SickScanApiRegisterLdmrsObjectArrayMsg(SickScanApiHandle apiHandle, SickScanLdmrsObjectArrayCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLdmrsObjectArrayMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_ldmrsobjectarray_messages.addListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::addLdmrsObjectArrayListener(node, ldmrsobjectarray_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLdmrsObjectArrayMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterLdmrsObjectArrayMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}
int32_t SickScanApiDeregisterLdmrsObjectArrayMsg(SickScanApiHandle apiHandle, SickScanLdmrsObjectArrayCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLdmrsObjectArrayMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_ldmrsobjectarray_messages.removeListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::removeLdmrsObjectArrayListener(node, ldmrsobjectarray_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLdmrsObjectArrayMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterLdmrsObjectArrayMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

// Register / deregister a callback for VisualizationMarker messages
int32_t SickScanApiRegisterVisualizationMarkerMsg(SickScanApiHandle apiHandle, SickScanVisualizationMarkerCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiRegisterVisualizationMarkerMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_visualizationmarker_messages.addListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::addVisualizationMarkerListener(node, visualizationmarker_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterVisualizationMarkerMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiRegisterVisualizationMarkerMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}
int32_t SickScanApiDeregisterVisualizationMarkerMsg(SickScanApiHandle apiHandle, SickScanVisualizationMarkerCallback callback)
{
    try
    {
        if (apiHandle == 0)
        {
            ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterVisualizationMarkerMsg(): invalid apiHandle");
            return SICK_SCAN_API_NOT_INITIALIZED;
        }
        s_callback_handler_visualizationmarker_messages.removeListener(apiHandle, callback);
        rosNodePtr node = castApiHandleToNode(apiHandle);
        sick_scan::removeVisualizationMarkerListener(node, visualizationmarker_callback);
        return SICK_SCAN_API_SUCCESS;
    }
    catch(const std::exception& e)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterVisualizationMarkerMsg(): exception " << e.what());
    }
    catch(...)
    {
        ROS_ERROR_STREAM("## ERROR SickScanApiDeregisterVisualizationMarkerMsg(): unknown exception ");
    }
    return SICK_SCAN_API_ERROR;
}

/*
*  Polling functions
*/

// Wait for and return the next cartesian resp. polar PointCloud messages. Note: SickScanApiWait...Msg() allocates a message. Use function SickScanApiFree...Msg() to deallocate it after use.
int32_t SickScanApiWaitNextCartesianPointCloudMsg(SickScanApiHandle apiHandle, SickScanPointCloudMsg* msg, double timeout_sec)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}
int32_t SickScanApiWaitNextPolarPointCloudMsg(SickScanApiHandle apiHandle, SickScanPointCloudMsg* msg, double timeout_sec)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}
int32_t SickScanApiFreePolarPointCloudMsg(SickScanApiHandle apiHandle, SickScanPointCloudMsg* msg)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}

// Wait for and return the next Imu messages. Note: SickScanApiWait...Msg() allocates a message. Use function SickScanApiFree...Msg() to deallocate it after use.
int32_t SickScanApiWaitNextImuMsg(SickScanApiHandle apiHandle, SickScanImuMsg* msg, double timeout_sec)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}
int32_t SickScanApiFreeImuMsg(SickScanApiHandle apiHandle, SickScanImuMsg* msg)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}

// Wait for and return the next LFErec messages. Note: SickScanApiWait...Msg() allocates a message. Use function SickScanApiFree...Msg() to deallocate it after use.
int32_t SickScanApiWaitNextLFErecMsg(SickScanApiHandle apiHandle, SickScanLFErecMsg* msg, double timeout_sec)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}
int32_t SickScanApiFreeLFErecMsg(SickScanApiHandle apiHandle, SickScanLFErecMsg* msg)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}

// Wait for and return the next LIDoutputstate messages. Note: SickScanApiWait...Msg() allocates a message. Use function SickScanApiFree...Msg() to deallocate it after use.
int32_t SickScanApiWaitNextLIDoutputstateMsg(SickScanApiHandle apiHandle, SickScanLIDoutputstateMsg* msg, double timeout_sec)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}
int32_t SickScanApiFreeLIDoutputstateMsg(SickScanApiHandle apiHandle, SickScanLIDoutputstateMsg* msg)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}

// Wait for and return the next RadarScan messages. Note: SickScanApiWait...Msg() allocates a message. Use function SickScanApiFree...Msg() to deallocate it after use.
int32_t SickScanApiWaitNextRadarScanMsg(SickScanApiHandle apiHandle, SickScanRadarScan* msg, double timeout_sec)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}
int32_t SickScanApiFreeRadarScanMsg(SickScanApiHandle apiHandle, SickScanRadarScan* msg)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}

// Wait for and return the next LdmrsObjectArray messages. Note: SickScanApiWait...Msg() allocates a message. Use function SickScanApiFree...Msg() to deallocate it after use.
int32_t SickScanApiWaitNextLdmrsObjectArrayMsg(SickScanApiHandle apiHandle, SickScanLdmrsObjectArray* msg, double timeout_sec)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}
int32_t SickScanApiFreeLdmrsObjectArrayMsg(SickScanApiHandle apiHandle, SickScanLdmrsObjectArray* msg)
{
    return SICK_SCAN_API_NOT_IMPLEMENTED;
}