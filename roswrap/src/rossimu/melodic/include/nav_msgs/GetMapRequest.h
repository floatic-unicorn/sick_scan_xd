// Generated by gencpp from file nav_msgs/GetMapRequest.msg
// DO NOT EDIT!


#ifndef NAV_MSGS_MESSAGE_GETMAPREQUEST_H
#define NAV_MSGS_MESSAGE_GETMAPREQUEST_H


#include <string>
#include <vector>
#include <map>

#include <ros/types.h>
#include <ros/serialization.h>
#include <ros/builtin_message_traits.h>
#include <ros/message_operations.h>


namespace nav_msgs
{
template <class ContainerAllocator>
struct GetMapRequest_
{
  typedef GetMapRequest_<ContainerAllocator> Type;

  GetMapRequest_()
    {
    }
  GetMapRequest_(const ContainerAllocator& _alloc)
    {
  (void)_alloc;
    }







  typedef boost::shared_ptr< ::nav_msgs::GetMapRequest_<ContainerAllocator> > Ptr;
  typedef boost::shared_ptr< ::nav_msgs::GetMapRequest_<ContainerAllocator> const> ConstPtr;

}; // struct GetMapRequest_

typedef ::nav_msgs::GetMapRequest_<std::allocator<void> > GetMapRequest;

typedef boost::shared_ptr< ::nav_msgs::GetMapRequest > GetMapRequestPtr;
typedef boost::shared_ptr< ::nav_msgs::GetMapRequest const> GetMapRequestConstPtr;

// constants requiring out of line definition



template<typename ContainerAllocator>
std::ostream& operator<<(std::ostream& s, const ::nav_msgs::GetMapRequest_<ContainerAllocator> & v)
{
ros::message_operations::Printer< ::nav_msgs::GetMapRequest_<ContainerAllocator> >::stream(s, "", v);
return s;
}


} // namespace nav_msgs

namespace ros
{
namespace message_traits
{





template <class ContainerAllocator>
struct IsFixedSize< ::nav_msgs::GetMapRequest_<ContainerAllocator> >
  : TrueType
  { };

template <class ContainerAllocator>
struct IsFixedSize< ::nav_msgs::GetMapRequest_<ContainerAllocator> const>
  : TrueType
  { };

template <class ContainerAllocator>
struct IsMessage< ::nav_msgs::GetMapRequest_<ContainerAllocator> >
  : TrueType
  { };

template <class ContainerAllocator>
struct IsMessage< ::nav_msgs::GetMapRequest_<ContainerAllocator> const>
  : TrueType
  { };

template <class ContainerAllocator>
struct HasHeader< ::nav_msgs::GetMapRequest_<ContainerAllocator> >
  : FalseType
  { };

template <class ContainerAllocator>
struct HasHeader< ::nav_msgs::GetMapRequest_<ContainerAllocator> const>
  : FalseType
  { };


template<class ContainerAllocator>
struct MD5Sum< ::nav_msgs::GetMapRequest_<ContainerAllocator> >
{
  static const char* value()
  {
    return "d41d8cd98f00b204e9800998ecf8427e";
  }

  static const char* value(const ::nav_msgs::GetMapRequest_<ContainerAllocator>&) { return value(); }
  static const uint64_t static_value1 = 0xd41d8cd98f00b204ULL;
  static const uint64_t static_value2 = 0xe9800998ecf8427eULL;
};

template<class ContainerAllocator>
struct DataType< ::nav_msgs::GetMapRequest_<ContainerAllocator> >
{
  static const char* value()
  {
    return "nav_msgs/GetMapRequest";
  }

  static const char* value(const ::nav_msgs::GetMapRequest_<ContainerAllocator>&) { return value(); }
};

template<class ContainerAllocator>
struct Definition< ::nav_msgs::GetMapRequest_<ContainerAllocator> >
{
  static const char* value()
  {
    return "# Get the map as a nav_msgs/OccupancyGrid\n"
;
  }

  static const char* value(const ::nav_msgs::GetMapRequest_<ContainerAllocator>&) { return value(); }
};

} // namespace message_traits
} // namespace ros

namespace ros
{
namespace serialization
{

  template<class ContainerAllocator> struct Serializer< ::nav_msgs::GetMapRequest_<ContainerAllocator> >
  {
    template<typename Stream, typename T> inline static void allInOne(Stream&, T)
    {}

    ROS_DECLARE_ALLINONE_SERIALIZER
  }; // struct GetMapRequest_

} // namespace serialization
} // namespace ros

namespace ros
{
namespace message_operations
{

template<class ContainerAllocator>
struct Printer< ::nav_msgs::GetMapRequest_<ContainerAllocator> >
{
  template<typename Stream> static void stream(Stream&, const std::string&, const ::nav_msgs::GetMapRequest_<ContainerAllocator>&)
  {}
};

} // namespace message_operations
} // namespace ros

#endif // NAV_MSGS_MESSAGE_GETMAPREQUEST_H