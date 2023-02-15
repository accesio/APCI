#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>

#define MAX_DEVICES 100

int df; // Global variable to store the device handle.

int OpenDevFile()
{
    char *devicefiles[MAX_DEVICES];
    int device_count = 0;
    const char *devicepath = "/dev/apci/";
    DIR *dir;
    struct dirent *entry;

    printf("Attempting to locate device files in %s\n", devicepath);

    dir = opendir(devicepath);
    if (dir == NULL)
    {
        printf("invalid: unable to open %s\nPerhaps the apci.ko is not loaded, or you need sudo?\n", devicepath);
        perror("opendir");
        return 0;
    }

    while ((entry = readdir(dir)) != NULL && device_count < MAX_DEVICES)
    {
        char devfile[512];
        struct stat st;

        snprintf(devfile, sizeof(devfile), "%s%s", devicepath, entry->d_name);

        // Use stat() to determine the file type.
        if (stat(devfile, &st) == 0)
        {
            // Check if it's a regular file or a symbolic link.
            if ( S_ISCHR(st.st_mode))
            {
                printf("Found Devicefile %s\n", entry->d_name);
                devicefiles[device_count] = strdup(devfile);
                if (devicefiles[device_count] == NULL)
                {
                    perror("strdup");
                    closedir(dir);
                    return 0;
                }
                device_count++;
            }
        }
    }

    closedir(dir);

    if (device_count == 0)
    {
        printf("No valid device files found in %s\n", devicepath);
        return 0;
    }
    else if (device_count == 1)
    {
        // If only one device, select it automatically.
        df = open(devicefiles[0], O_RDONLY);
        if (df >= 0)
        {
            printf("Success Opening device @ %s\n", devicefiles[0]);
        }
        else
        {
            perror("open");
        }
        free(devicefiles[0]);
    }
    else
    {
        // More than one device found, prompt user to select.
        printf("Multiple device files found:\n");
        for (int i = 0; i < device_count; i++)
        {
            printf("%d: %s\n", i + 1, devicefiles[i]);
        }

        int selection = 0;
        while (1)
        {
            printf("Select a device file to open (1-%d): ", device_count);
            if (scanf("%d", &selection) == 1 && selection >= 1 && selection <= device_count)
            {
                break;
            }
            else
            {
                printf("Invalid selection. Please try again.\n");
                while (getchar() != '\n'); // Clear the input buffer
            }
        }

        // Open the selected device file.
        df = open(devicefiles[selection - 1], O_RDONLY);
        if (df >= 0)
        {
            printf("Success Opening device @ %s\n", devicefiles[selection - 1]);
        }
        else
        {
            perror("open");
        }

        // Free the allocated memory.
        for (int i = 0; i < device_count; i++)
        {
            free(devicefiles[i]);
        }
    }
    return df;
}
