#!/usr/bin/env python3

import serial
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
import re

class SerialToCmdVel(Node):
    def __init__(self):
        super().__init__('serial_to_cmd_vel')
        
        # Publisher for cmd_vel
        self.cmd_vel_pub = self.create_publisher(Twist, '/turtle1/cmd_vel', 10)
        
        # Parameters (adjust these for your robot)
        self.declare_parameter('serial_port', '/dev/ttyUSB0')
        self.declare_parameter('baud_rate', 115200)
        self.declare_parameter('max_linear_speed', 0.5)  # m/s
        self.declare_parameter('max_angular_speed', 1.0) # rad/s
        
        serial_port = self.get_parameter('serial_port').value
        baud_rate = self.get_parameter('baud_rate').value
        self.max_linear_speed = self.get_parameter('max_linear_speed').value
        self.max_angular_speed = self.get_parameter('max_angular_speed').value
        
        # Scale factors (convert 0-100 range to velocities)
        self.linear_scale = self.max_linear_speed / 50.0   # Since center is 50
        self.angular_scale = self.max_angular_speed / 50.0
        
        # Serial connection
        try:
            self.ser = serial.Serial(
                serial_port, 
                baud_rate,
                timeout=0.01,  # Short timeout for responsiveness
                write_timeout=0
            )
            self.ser.reset_input_buffer()
            self.get_logger().info(f"Connected to serial port: {serial_port}")
        except serial.SerialException as e:
            self.get_logger().error(f"Failed to connect to serial port: {e}")
            return
        
        # Buffer for serial data
        self.buffer = ""
        
        # Timer for processing serial data (high frequency)
        self.timer = self.create_timer(0.01, self.process_serial)  # 100Hz
        
        self.get_logger().info("Serial to cmd_vel node started")
        self.get_logger().info(f"Max speeds - Linear: {self.max_linear_speed} m/s, Angular: {self.max_angular_speed} rad/s")
    
    def parse_serial_line(self, line):
        """Parse serial line in format: 'Linear:XX Angular:YY'"""
        try:
            # Use regex to extract numbers
            match = re.search(r'Linear:(\d+)\s+Angular:(\d+)', line)
            if match:
                linear_val = int(match.group(1))
                angular_val = int(match.group(2))
                return linear_val, angular_val
        except (ValueError, IndexError) as e:
            self.get_logger().warn(f"Failed to parse line: {line}")
        return None, None
    
    def convert_to_cmd_vel(self, linear_val, angular_val):
        """Convert 0-100 values to cmd_vel message"""
        twist = Twist()
        
        # Convert linear (0-100, center 50) to m/s
        # 50 = stop, 0 = max reverse, 100 = max forward
        linear_cmd = (linear_val - 50) * self.linear_scale
        twist.linear.x = linear_cmd
        
        # Convert angular (0-100, center 50) to rad/s
        # 50 = straight, 0 = max left, 100 = max right
        angular_cmd = (angular_val - 50) * self.angular_scale
        twist.angular.z = -angular_cmd  # Negative for proper direction
        
        return twist
    
    def process_serial(self):
        """Process incoming serial data"""
        try:
            # Read all available bytes
            if self.ser.in_waiting > 0:
                data = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                self.buffer += data
                
                # Process complete lines
                while '\n' in self.buffer:
                    line, self.buffer = self.buffer.split('\n', 1)
                    line = line.strip()
                    
                    if line.startswith("Linear:") and "Angular:" in line:
                        linear_val, angular_val = self.parse_serial_line(line)
                        
                        if linear_val is not None and angular_val is not None:
                            # Convert to cmd_vel and publish
                            twist_msg = self.convert_to_cmd_vel(linear_val, angular_val)
                            self.cmd_vel_pub.publish(twist_msg)
                            
                            # Log at reduced rate to avoid spam
                            self.get_logger().info(
                                f"L:{linear_val}->{twist_msg.linear.x:.2f}m/s "
                                f"A:{angular_val}->{twist_msg.angular.z:.2f}rad/s",
                                throttle_duration_sec=1.0
                            )
        
        except Exception as e:
            self.get_logger().error(f"Serial processing error: {e}")
            # Reset buffer on error
            self.buffer = ""
    
    def destroy_node(self):
        """Cleanup"""
        if hasattr(self, 'ser') and self.ser.is_open:
            self.ser.close()
        super().destroy_node()

def main(args=None):
    rclpy.init(args=args)
    node = SerialToCmdVel()
    
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
