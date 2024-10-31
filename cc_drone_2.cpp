#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <string>
#include <atomic>
#include <chrono>
#include <mutex>
#include <fstream>
#include <vector>

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

std::atomic<bool> is_connected(false);
std::pair<double, double> position{0.0, 0.0}; // Initial position (0, 0)
std::mutex position_mutex;

// XOR Encryption/Decryption Function
std::string xor_cipher(const std::string &data, char key)
{
    std::string result = data;
    for (auto &c : result)
        c ^= key;
    return result;
}

void update_position(const std::string &command)
{
    std::lock_guard<std::mutex> lock(position_mutex);

    if (command == "move front")
    {
        position.second += 1.0; // Increment y-coordinate
    }
    else if (command == "move back")
    {
        position.second -= 1.0; // Decrement y-coordinate
    }
    else if (command == "move right")
    {
        position.first += 1.0; // Increment x-coordinate
    }
    else if (command == "move left")
    {
        position.first -= 1.0; // Decrement x-coordinate
    }

    std::cout << "Drone moved to position (" << position.first << ", " << position.second << ")" << std::endl;
}

void receive_control_commands(boost::asio::io_context &io_context, unsigned short port, int drone_id)
{
    udp::socket socket(io_context, udp::endpoint(udp::v4(), port));
    socket.set_option(boost::asio::socket_base::reuse_address(true));
    std::cout << "Drone " << drone_id << " Control Command Receiver started on port " << port << std::endl;

    udp::endpoint sender_endpoint;
    std::vector<char> data(1024);

    while (true)
    {
        try
        {
            size_t len = socket.receive_from(boost::asio::buffer(data), sender_endpoint);
            for (auto c : data)
            {
                std::cout << c;
            }
            std::cout << std::endl;
            std::string command(data.data(), len);
            std::cout << "Drone " << drone_id << " Received command: " << command << std::endl;
            update_position(command); // Update position based on the command
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error receiving command: " << e.what() << std::endl;
        }
    }
}

void send_telemetry_data(boost::asio::io_context &io_context, const std::string &server_ip, unsigned short port, int drone_id)
{
    while (true)
    {
        try
        {
            tcp::socket socket(io_context);
            tcp::endpoint server_endpoint(boost::asio::ip::make_address(server_ip), port);
            std::cout << "Drone " << drone_id << " attempting to connect to IP: " << server_ip << ", Port: " << port << std::endl;
            socket.connect(server_endpoint);
            is_connected.store(true);
            std::cout << "Drone " << drone_id << " Connected to server for telemetry data." << std::endl;

            while (is_connected.load())
            {
                double x, y;
                {
                    std::lock_guard<std::mutex> lock(position_mutex);
                    x = position.first;
                    y = position.second;
                }
                std::string data = "Telemetry data from Drone " + std::to_string(drone_id) +
                                   " - Position: (" + std::to_string(x) + ", " + std::to_string(y) + ")";
                boost::asio::write(socket, boost::asio::buffer(data + "\n"));
                std::cout << "Drone " << drone_id << " Sent telemetry data: " << data << std::endl;
                std::this_thread::sleep_for(std::chrono::minutes(3)); // Send every 3 minutes
            }
            socket.close();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception in send_telemetry_data (Drone " << drone_id << "): " << e.what() << std::endl;
            is_connected.store(false);
            std::this_thread::sleep_for(std::chrono::seconds(10)); // Wait before retrying
        }
    }
}

void send_large_file_tcp(const std::string &file_path, const std::string &server_ip, unsigned short port, int drone_id)
{
    while (true) // Infinite loop to periodically send the file
    {
        std::this_thread::sleep_for(std::chrono::minutes(1)); // Wait for 5 minutes after connection is established

        if (is_connected.load()) // Only send the file if connected
        {
            try
            {
                boost::asio::io_context io_context;
                tcp::socket socket(io_context);
                tcp::endpoint server_endpoint(boost::asio::ip::make_address(server_ip), port);

                socket.connect(server_endpoint);
                std::cout << "Drone " << drone_id << " Connected to server for file transfer." << std::endl;

                std::ifstream file(file_path, std::ios::binary);
                if (!file)
                {
                    std::cerr << "Drone " << drone_id << " Error opening file: " << file_path << std::endl;
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
                        std::cerr << "Drone " << drone_id << " Error sending data: " << error.message() << std::endl;
                        break;
                    }

                    std::cout << "Drone " << drone_id << " Sent chunk of " << bytes_sent << " bytes." << std::endl;
                }

                std::cout << "Drone " << drone_id << " File transfer completed." << std::endl;

                // Clean up: Shutdown and close the socket gracefully
                boost::system::error_code shutdown_error;
                socket.shutdown(boost::asio::socket_base::shutdown_both, shutdown_error);
                if (shutdown_error)
                {
                    std::cerr << "Drone " << drone_id << " Error during shutdown: " << shutdown_error.message() << std::endl;
                }

                boost::system::error_code close_error;
                socket.close(close_error);
                if (close_error)
                {
                    std::cerr << "Drone " << drone_id << " Error during socket close: " << close_error.message() << std::endl;
                }

                file.close();
            }
            catch (const std::exception &e)
            {
                std::cerr << "Drone " << drone_id << " Exception: " << e.what() << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::minutes(5)); // Wait for 5 minutes before the next file transfer
    }
}

int main()
{
    unsigned short telemetry_port = 9001;
    unsigned short control_port = 9002;
    unsigned short file_transfer_port = 9004;
    int drone_id = 2; // Change to 2 for the second drone

    boost::asio::io_context io_context;
    std::string server_ip = "127.0.0.1"; // Replace with the actual server IP address

    std::string file_path = "./big_file.txt";

    std::thread control_thread(receive_control_commands, std::ref(io_context), control_port, drone_id);
    std::thread telemetry_thread(send_telemetry_data, std::ref(io_context), server_ip, telemetry_port, drone_id);
    std::thread file_transfer_thread(send_large_file_tcp, file_path, server_ip, file_transfer_port, drone_id);

    control_thread.join();
    telemetry_thread.join();
    file_transfer_thread.join();

    return 0;
}
