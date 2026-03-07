/**
 * @file cli_test_base.cc
 * @brief Implementation of CLI test base fixture
 */

#include "cli_test_base.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>

namespace taurus {
namespace test {

namespace fs = std::filesystem;

void CLITestBase::SetUp() {
    // Find CLI binary - use absolute path from build directory
    // Get the project root by going up from build/test/
    fs::path current = fs::current_path();
    fs::path build_dir = current;

    // If we're in build/test/, go up to build/
    if (build_dir.filename() == "test") {
        build_dir = build_dir.parent_path();
    }

    cli_path_ = (build_dir / "cli" / "taurus").string();

    // Check if CLI exists
    if (!fs::exists(cli_path_)) {
        // Try alternate location
        cli_path_ = (build_dir / "taurus").string();
    }

    ASSERT_TRUE(fs::exists(cli_path_))
        << "CLI binary not found at: " << cli_path_;

    // Find fixtures directory
    // Check in test directory first (ctest working directory)
    fixtures_dir_ = (current / "fixtures").string();
    if (!fs::is_directory(fixtures_dir_)) {
        // Try alternate location: build/cli/fixtures
        fixtures_dir_ = (build_dir / "cli" / "fixtures").string();
    }
    if (!fs::is_directory(fixtures_dir_)) {
        // Try: build/test/cli/fixtures
        fixtures_dir_ = (build_dir / "test" / "cli" / "fixtures").string();
    }

    ASSERT_TRUE(fs::is_directory(fixtures_dir_))
        << "Fixtures directory not found: " << fixtures_dir_;
}

void CLITestBase::TearDown() {
    // Clean up temporary files
    for (const auto& file : temp_files_) {
        if (fs::exists(file)) {
            fs::remove(file);
        }
    }
    temp_files_.clear();
}

CLIResult CLITestBase::Execute(const std::vector<std::string>& args) {
    CLIResult result;
    
    // Create pipes for stdout and stderr
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.exit_code = -1;
        return result;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        result.exit_code = -1;
        return result;
    }
    
    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);  // Close read end
        close(stderr_pipe[0]);
        
        // Redirect stdout and stderr
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Prepare arguments
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(cli_path_.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        // Execute
        execv(cli_path_.c_str(), argv.data());
        
        // If we get here, exec failed
        exit(127);
    } else {
        // Parent process
        close(stdout_pipe[1]);  // Close write end
        close(stderr_pipe[1]);
        
        // Read stdout
        char buffer[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
            result.stdout_output.append(buffer, n);
        }
        close(stdout_pipe[0]);
        
        // Read stderr
        while ((n = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
            result.stderr_output.append(buffer, n);
        }
        close(stderr_pipe[0]);
        
        // Wait for child
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else {
            result.exit_code = -1;
        }
    }
    
    return result;
}

CLIResult CLITestBase::ExecuteWithStdin(const std::vector<std::string>& args,
                                        const std::string& stdin_data) {
    CLIResult result;
    
    // Create pipes
    int stdin_pipe[2];
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.exit_code = -1;
        return result;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        result.exit_code = -1;
        return result;
    }
    
    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);   // Close write end of stdin
        close(stdout_pipe[0]);  // Close read end
        close(stderr_pipe[0]);
        
        // Redirect stdin, stdout, stderr
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Prepare arguments
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(cli_path_.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        // Execute
        execv(cli_path_.c_str(), argv.data());
        
        // If we get here, exec failed
        exit(127);
    } else {
        // Parent process
        close(stdin_pipe[0]);   // Close read end of stdin
        close(stdout_pipe[1]);  // Close write end
        close(stderr_pipe[1]);
        
        // Write stdin data
        const char* data = stdin_data.c_str();
        size_t remaining = stdin_data.size();
        while (remaining > 0) {
            ssize_t written = write(stdin_pipe[1], data, remaining);
            if (written <= 0) break;
            data += written;
            remaining -= written;
        }
        close(stdin_pipe[1]);  // Close stdin to signal EOF
        
        // Read stdout
        char buffer[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
            result.stdout_output.append(buffer, n);
        }
        close(stdout_pipe[0]);
        
        // Read stderr
        while ((n = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
            result.stderr_output.append(buffer, n);
        }
        close(stderr_pipe[0]);
        
        // Wait for child
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else {
            result.exit_code = -1;
        }
    }
    
    return result;
}

std::string CLITestBase::FixturePath(const std::string& filename) {
    return (fs::path(fixtures_dir_) / filename).string();
}

std::string CLITestBase::ReadFixture(const std::string& filename) {
    std::string path = FixturePath(filename);
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "Failed to open fixture: " << path;
    
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

std::string CLITestBase::CreateTempFile(const std::string& content) {
    char temp_name[] = "/tmp/taurus_test_XXXXXX";
    int fd = mkstemp(temp_name);
    EXPECT_GE(fd, 0) << "Failed to create temporary file";
    
    if (fd >= 0) {
        write(fd, content.c_str(), content.size());
        close(fd);
        
        temp_files_.push_back(temp_name);
        return temp_name;
    }
    
    return "";
}

void CLITestBase::AssertSuccess(const CLIResult& result) {
    ASSERT_EQ(result.exit_code, 0)
        << "Command failed with output:\n"
        << "STDOUT:\n" << result.stdout_output << "\n"
        << "STDERR:\n" << result.stderr_output;
}

void CLITestBase::AssertFailure(const CLIResult& result, int expected_code) {
    ASSERT_EQ(result.exit_code, expected_code)
        << "Expected exit code " << expected_code
        << " but got " << result.exit_code << "\n"
        << "STDOUT:\n" << result.stdout_output << "\n"
        << "STDERR:\n" << result.stderr_output;
}

void CLITestBase::AssertContains(const std::string& text, 
                                 const std::string& pattern) {
    ASSERT_NE(text.find(pattern), std::string::npos)
        << "Text does not contain pattern: '" << pattern << "'\n"
        << "Actual text:\n" << text;
}

void CLITestBase::AssertNotContains(const std::string& text,
                                    const std::string& pattern) {
    ASSERT_EQ(text.find(pattern), std::string::npos)
        << "Text unexpectedly contains pattern: '" << pattern << "'\n"
        << "Actual text:\n" << text;
}

} // namespace test
} // namespace taurus