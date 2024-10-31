#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <string>
#include <atomic>
#include <fstream>
#include <vector>
#include <chrono>

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

// XOR Encryption/Decryption Function
std::string xor_cipher(const std::string &data, char key)
{
    std::string result = data;
    for (auto &c : result)
        c ^= key;
    return result;
}

// Drone's position coordinates
int x = 0, y = 0;

// Atomic flag to signal connection status
std::atomic<bool> is_connected(false);

// Function to receive and process control commands from the server
void receive_control_commands(boost::asio::io_context &io_context, unsigned short port, char key)
{
    udp::socket socket(io_context, udp::endpoint(udp::v4(), port));
    std::cout << "Control Command Receiver started on port " << port << std::endl;

    udp::endpoint sender_endpoint;

    while (true) // Infinite loop to continuously receive commands
    {
        try
        {
            std::vector<char> data(1024);
            boost::system::error_code error;

            // Receive the command from the server
            size_t length = socket.receive_from(boost::asio::buffer(data), sender_endpoint, 0, error);

            if (error && error != boost::asio::error::message_size)
            {
                std::cerr << "Error receiving control command: " << error.message() << std::endl;
                continue;
            }

            // Create a std::string from the received data
            std::string encrypted_command(data.data(), length);

            encrypted_command.erase(std::remove(encrypted_command.begin(), encrypted_command.end(), '\n'), encrypted_command.end());
            std::string command = xor_cipher(encrypted_command, key);

            // Update drone position based on the command
            if (command == "move front")
                y += 1;
            else if (command == "move back")
                y -= 1;
            else if (command == "move left")
                x -= 1;
            else if (command == "move right")
                x += 1;

            // Display updated position
            std::cout << "Received command: " << command << ". Updated position: (" << x << ", " << y << ")" << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }
}

// Function to send telemetry data to the server
void send_telemetry_data(boost::asio::io_context &io_context, const std::string &server_ip, unsigned short port, char key)
{
    try
    {
        tcp::socket socket(io_context);
        tcp::endpoint server_endpoint(boost::asio::ip::make_address(server_ip), port);

        // Attempt to connect to the server
        socket.connect(server_endpoint);
        is_connected.store(true); // Set connection status
        std::cout << "Connected to server for telemetry data." << std::endl;

        while (true) // Infinite loop to continuously send telemetry data
        {
            // Create a string representation of the current drone position
            std::string position = "Position: (" + std::to_string(x) + ", " + std::to_string(y) + ")";
            std::string encrypted_data = xor_cipher(position, key) + "\n"; // Encrypt and add newline character

            boost::system::error_code error;
            size_t bytes_sent = boost::asio::write(socket, boost::asio::buffer(encrypted_data), error);

            if (error)
            {
                std::cerr << "Error sending data: " << error.message() << std::endl;

                // Handle connection errors
                if (error == boost::asio::error::eof ||
                    error == boost::asio::error::connection_reset ||
                    error == boost::asio::error::connection_aborted)
                {
                    std::cerr << "Connection issues detected. Attempting to reconnect..." << std::endl;

                    socket.close();
                    is_connected.store(false); // Update connection status

                    // Attempt reconnection
                    std::this_thread::sleep_for(std::chrono::seconds(10)); // Wait before retrying
                    socket.connect(server_endpoint, error);
                    if (!error)
                    {
                        std::cout << "Reconnected successfully." << std::endl;
                        is_connected.store(true); // Restore connection status
                    }
                }
                continue;
            }

            std::cout << "Sent telemetry data: " << position << " (" << bytes_sent << " bytes)." << std::endl;

            // Delay between telemetry updates (adjust as needed)
            std::this_thread::sleep_for(std::chrono::seconds(60)); // Sends position every 1 minute
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

// Function to send a large file periodically using TCP
void send_large_file_tcp(const std::string &file_path, const std::string &server_ip, unsigned short port)
{
    while (true) // Infinite loop to periodically send the file
    {
        // Wait for 5 minutes after connection is established
        std::this_thread::sleep_for(std::chrono::minutes(5));

        if (is_connected.load()) // Only send the file if connected
        {
            try
            {
                boost::asio::io_context io_context;
                tcp::socket socket(io_context);
                tcp::endpoint server_endpoint(boost::asio::ip::make_address(server_ip), port);

                socket.connect(server_endpoint);
                std::cout << "Connected to server for file transfer." << std::endl;

                std::ifstream file(file_path, std::ios::binary);
                if (!file)
                {
                    std::cerr << "Error opening file: " << file_path << std::endl;
                    return;
                }

                std::vector<char> buffer(1024); // Buffer for reading file chunks

                while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0)
                {
                    size_t bytes_read = file.gcount();
                    boost::system::error_code error;
                    size_t bytes_sent = boost::asio::write(socket, boost::asio::buffer(buffer.data(), bytes_read), error);

                    if (error)
                    {
                        std::cerr << "Error sending data: " << error.message() << std::endl;
                        break;
                    }

                    std::cout << "Sent chunk of " << bytes_sent << " bytes." << std::endl;
                }

                std::cout << "File transfer completed." << std::endl;

                // Clean up: Shutdown and close the socket gracefully
                boost::system::error_code shutdown_error;
                socket.shutdown(boost::asio::socket_base::shutdown_both, shutdown_error);
                if (shutdown_error)
                {
                    std::cerr << "Error during shutdown: " << shutdown_error.message() << std::endl;
                }

                boost::system::error_code close_error;
                socket.close(close_error);
                if (close_error)
                {
                    std::cerr << "Error during socket close: " << close_error.message() << std::endl;
                }

                file.close();
            }
            catch (const std::exception &e)
            {
                std::cerr << "Exception: " << e.what() << std::endl;
            }
        }

        // Wait for 5 minutes before the next file transfer
        std::this_thread::sleep_for(std::chrono::minutes(5));
    }
}

int main()
{
    char key = 0x42; // XOR cipher key
    unsigned short control_port = 9000;
    unsigned short telemetry_port = 9001;
    unsigned short file_transfer_port = 9002; // Port for file transfer

    boost::asio::io_context io_context;

    // Replace with the path to the large file you want to send
    std::string file_path = "./big_file.txt";
    std::string server_ip = "127.0.0.1"; // Replace with the actual server IP address

    std::thread control_thread(receive_control_commands, std::ref(io_context), control_port, key);
    std::thread telemetry_thread(send_telemetry_data, std::ref(io_context), server_ip, telemetry_port, key);
    std::thread file_transfer_thread(send_large_file_tcp, file_path, server_ip, file_transfer_port);

    control_thread.join();
    telemetry_thread.join();
    file_transfer_thread.join();

    std::cout << "Exiting program." << std::endl;
    return 0;
}
