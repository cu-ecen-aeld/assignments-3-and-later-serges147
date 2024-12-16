#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

int main(const int argc, const char** const argv)
{
    // Verify the number of arguments.
    //
    openlog(NULL, 0, LOG_USER);
    if (argc != 3)
    {
        syslog(LOG_ERR, "Invalid Number of args: %d", argc);
        return 1;
    }
    const char* const file_path = argv[1];
    const char* const write_str = argv[2];

    // Open the file for writing (overwriting if exists).
    //
    FILE* const file = fopen(file_path, "w");
    if (file == NULL)
    {
        const int err_code = errno;
        syslog(LOG_ERR, "Failed to open file: '%s'. Error (%d): %s", file_path, err_code, strerror(err_code));
        return 1;
    }

    // Write the string to the file.
    //
    syslog(LOG_DEBUG, "Writing %s to %s", write_str, file_path);
    const size_t str_len = strlen(write_str);
    if (str_len != fwrite(write_str, 1, str_len, file))
    {
        const int err_code = errno;
        syslog(LOG_ERR, "Failed to write to file: '%s'. Error (%d): %s", file_path, err_code, strerror(err_code));
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}