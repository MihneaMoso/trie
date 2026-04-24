#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
    #define PATH_SEP "\\"
#else
    #define PATH_SEP "/"
#endif

static size_t reclevel = 0;

void print_usage(char* program) {
    printf("Usage: %s [DIRECTORY]\nPrint filesystem tree of the specified directory.\n");
}

void walk_dir(const char* dirpath) {
    DIR* dirp = opendir(dirpath);
    if (dirp == NULL) {
        char *error = strerror(errno);
        printf("Opening directory unsuccessful, error: %s\n", error);

        exit(1);
    };

    struct dirent* root;
    
    while ((root = readdir(dirp)) != NULL) {
        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s"PATH_SEP"%s", dirpath, root->d_name);

        if (!(strcmp(root->d_name, ".") == 0 || strcmp(root->d_name, "..") == 0)) {
            printf("|");
            for (int i = 0; i <= reclevel; ++i) {
                printf("-");
            }
            printf("%s\n", root->d_name);
        }

        switch (root->d_type) {
            case DT_REG: {
                // printf("%s is a regular file, reclevel = %zu.\n", root->d_name, reclevel);
                break;
            }
            case DT_DIR: {
                char* name = root->d_name;
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                    continue;
                }
                reclevel += 1;

                walk_dir(fullpath);
                reclevel -= 1;
                
                break;
            }
            case DT_BLK:
            case DT_CHR:
            case DT_FIFO:
            case DT_LNK:
            case DT_SOCK:
            case DT_UNKNOWN:
                break;
       }
    }
    
    int ret = closedir(dirp);
    if (ret != 0) {
        char *error = strerror(errno);
        printf("Closing directory unsuccessful, error: %s\n", error);
    }
}


int main(int argc, char** argv) {

    char* dir;
    if (argc == 1) {
        dir = ".";
    } else if (argc == 2) {
        dir = argv[1];
        
    } else {
        print_usage(argv[0]);
        exit(1);
    }
    
    if (strlen(dir) >= 256) {
        printf("Please provide a directory with maximum %d length.\n", PATH_MAX);
        return 1;
    }


    walk_dir(dir);

    return 0;
}
