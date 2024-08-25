import socket
import message_pb2  # This is your generated protobuf file
import matplotlib.pyplot as plt
import numpy as np

def plot_radar(ax, position_1, position_2, distance):
    # Clear the existing plot
    ax.clear()

    # Convert positions and distance to radians
    theta_1 = np.radians(position_1)
    theta_2 = np.radians(position_2)

    # Plot data on the radar
    ax.plot([theta_1, theta_2], [0, distance], marker='o', color='r')
    ax.fill([theta_1, theta_2], [0, distance], color='r', alpha=0.3)

    # Set the plot title and labels
    ax.set_title("Radar Plot")
    ax.set_rmax(distance + 50)  # Set the max radius to the distance plus some margin
    ax.grid(True)

    # Redraw the plot
    plt.draw()
    plt.pause(0.001)

def main():
    # Initialize the plot
    plt.ion()  # Turn on interactive mode
    fig = plt.figure()
    ax = fig.add_subplot(111, polar=True)

    # Server connection details
    host = '192.168.0.2'  # Replace with your STM32 IP address
    port = 7

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.connect((host, port))
            print("Connected to server")
        except socket.error as e:
            print(f"Failed to connect: {e}")
            return
        
        while True:
            # Receive the data from the STM32
            data = s.recv(1024)  # Adjust buffer size if necessary

            if data:
                # Deserialize the received protobuf message
                received_combined = message_pb2.CombinedData()
                received_combined.ParseFromString(data)

                # Extract values from the protobuf message
                position_1 = received_combined.servo_data.position_1
                position_2 = received_combined.servo_data.position_2
                distance = received_combined.servo_data.distance

                # Print the received data in the console
                print(f"Received data - Position 1: {position_1}, Position 2: {position_2}, Distance: {distance}")

                # Plot the radar
                plot_radar(ax, position_2, position_1, distance)

            else:
                print("No data received")

if __name__ == "__main__":
    main()
