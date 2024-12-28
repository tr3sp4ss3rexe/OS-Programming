#include "fs.h"

FS::FS()
{
    cout << "Run help to see the available commands\n";
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    this->currentDir = "/";
    this->currentBlock = 0;
    this->current_directory_block = 0;
}

FS::~FS()
{


}

// formats the disk, i.e., creates an empty file system
int FS::format() {

    // Start by zeroing out the whole thing
    uint8_t zero_block[BLOCK_SIZE] = {0};

    for (int i = 0; i < 2048; i++) {
        disk.write(i, zero_block); 
    }

    const int fat_entries = BLOCK_SIZE / 2;

    // Get FAT ready
    for (int i = 0; i < fat_entries; i++) {
        fat[i] = FAT_FREE;
    }

    // Mark ROOT_BLOCK and FAT_BLOCK as reserved each entry is only 2 bytes.
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;  

    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        memset(root_dir[i].file_name, 0, sizeof(root_dir[i].file_name));
        root_dir[i].size = 0;
        root_dir[i].first_blk = 0;
        root_dir[i].type = TYPE_FILE;        
        root_dir[i].access_rights = 0;       
    }

    disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));
    
    this->currentDir = "/";
    this->currentBlock = 0;
    this->current_directory_block = 0;

    return 0;
}

int FS::resolvePathToDirectory(const string &path){
            if (path.empty()) {
            return this->currentBlock;
        }

        int startBlock = (path[0] == '/') ? ROOT_BLOCK : this->currentBlock;

        stringstream ss(path);
        string token;
        int dirBlock = startBlock;

        while (getline(ss, token, '/')) {
            if (token.empty()) {
                continue;
            }

            dir_entry entries[ROOT_DIR_SIZE];
            this->disk.read(dirBlock, reinterpret_cast<uint8_t*>(entries));

            if (token == "..") {
                // Move to parent directory
                bool foundParent = false;
                for (int i = 0; i < ROOT_DIR_SIZE; i++) {
                    if (strcmp(entries[i].file_name, "..") == 0 && entries[i].type == TYPE_DIR) {
                        dirBlock = entries[i].first_blk;
                        foundParent = true;
                        break;
                    }
                }
                // To the sub directory
                if (!foundParent) {
                    cerr << "[ERROR] Could not find parent directory.\n";
                    return -1;
                }
            } else {
                // Sub directory
                bool found = false;
                for (int i = 0; i < ROOT_DIR_SIZE; i++) {
                    if (strcmp(entries[i].file_name, token.c_str()) == 0 && entries[i].type == TYPE_DIR) {
                        dirBlock = entries[i].first_blk;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    cerr << "[ERROR] Directory '" << token << "' not found.\n";
                    return -1;
                }
            }
        }
        current_directory_block = dirBlock;
        return dirBlock;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(string filepath) {

    // Separate directory path and filename
    string directoryPath;
    string filename = filepath;
    size_t lastSlash = filepath.find_last_of('/');
    if (lastSlash != string::npos) {
        directoryPath = filepath.substr(0, lastSlash);
        filename = filepath.substr(lastSlash + 1);
        if (directoryPath.empty()) {
            directoryPath = "/";
        }
    } else {
        directoryPath = "";
    }

    int targetDirBlock = resolvePathToDirectory(directoryPath);
    if (targetDirBlock == -1) {
        cerr << "[ERROR] Failed to resolve directory path.\n";
        return -1;
    }


    dir_entry currentDir[ROOT_DIR_SIZE];
    this->disk.read(targetDirBlock, reinterpret_cast<uint8_t*>(currentDir));

    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(currentDir[i].file_name, filename.c_str()) == 0 && currentDir[i].file_name[0] != '\0') {
            cerr << "[ERROR] File '" << filename << "' already exists.\n";
            return -1;
        }
    }

    // Check if the directory has write permissions
    bool writePermission = false;

    if (targetDirBlock == ROOT_BLOCK) {
        writePermission = true;
    } else {
        int parentBlock = -1;
        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (strcmp(currentDir[i].file_name, "..") == 0 && currentDir[i].type == TYPE_DIR) {
                parentBlock = currentDir[i].first_blk;
                break;
            }
        }

        if (parentBlock == -1) {
            cerr << "[ERROR] Could not find the parent directory.\n";
            return -1;
        }

        // Read the parent directory
        dir_entry parentDir[ROOT_DIR_SIZE];
        this->disk.read(parentBlock, reinterpret_cast<uint8_t*>(parentDir));

        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (parentDir[i].type == TYPE_DIR && parentDir[i].first_blk == (uint16_t)targetDirBlock) {
                if ((parentDir[i].access_rights & WRITE) != 0) {
                    writePermission = true;
                }
                break;
            }
        }
    }

    if (!writePermission) {
        cerr << "[ERROR] Current directory does not have write access.\n";
        return -1;
    }


    vector<char> fileData;
    {
        bool lastWasNewline = false;
        while (true) {
            int c = cin.get();
            if (c == EOF) {
                break;
            }
            char ch = (char)c;
            if (ch == '\n') {
                if (lastWasNewline) {
                    break;
                }
                lastWasNewline = true;
            } else {
                lastWasNewline = false;
            }
            fileData.push_back(ch);
        }
    }


    dir_entry fileInfo;
    memset(&fileInfo, 0, sizeof(dir_entry));

    if (filename.length() > sizeof(fileInfo.file_name) - 1) {
        cerr << "[ERROR] Filename too long.\n";
        return -1;
    }

    strncpy(fileInfo.file_name, filename.c_str(), sizeof(fileInfo.file_name) - 1);
    fileInfo.size = (uint32_t)fileData.size();
    fileInfo.type = TYPE_FILE;
    fileInfo.access_rights = READ | WRITE;

    // Calculate how many blocks are needed
    const int fat_entries = BLOCK_SIZE / 2;
    int blocksNeeded = (fileInfo.size == 0) ? 1 : (int)((fileInfo.size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    // Find free blocks in FAT
    int startBlockIndex = -1;
    for (int i = 2; i < fat_entries; i++) {
        if (this->fat[i] == FAT_FREE) {
            startBlockIndex = i;
            break;
        }
    }
    if (startBlockIndex == -1) {
        cerr << "[ERROR] No free blocks available.\n";
        return -1;
    }

    fileInfo.first_blk = (uint16_t)startBlockIndex;

    int currentFatBlock = startBlockIndex;
    for (int j = 0; j < blocksNeeded; j++) {
        if (j == blocksNeeded - 1) {
            this->fat[currentFatBlock] = FAT_EOF;
        } else {
            int nextFreeBlock = -1;
            for (int k = currentFatBlock + 1; k < fat_entries; k++) {
                if (this->fat[k] == FAT_FREE) {
                    nextFreeBlock = k;
                    break;
                }
            }
            if (nextFreeBlock == -1) {
                cerr << "[ERROR] Not enough blocks for this large file.\n";
                int rb = startBlockIndex;
                while (rb != FAT_EOF && rb != FAT_FREE) {
                    int nxt = this->fat[rb];
                    this->fat[rb] = FAT_FREE;
                    if (nxt == FAT_EOF || nxt == FAT_FREE) break;
                    rb = nxt;
                }
                return -1;
            }
            this->fat[currentFatBlock] = nextFreeBlock;
            currentFatBlock = nextFreeBlock;
        }
    }

    this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));

    {
        uint8_t dirBuffer[BLOCK_SIZE] = {0};
        bool inserted = false;
        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (currentDir[i].file_name[0] == '\0' && currentDir[i].first_blk == 0) {
                currentDir[i] = fileInfo;
                memcpy(dirBuffer, currentDir, sizeof(currentDir));
                this->disk.write(targetDirBlock, dirBuffer);
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            cerr << "[ERROR] No space in target directory.\n";
            int rb = fileInfo.first_blk;
            while (rb != FAT_EOF && rb != FAT_FREE) {
                int nxt = this->fat[rb];
                this->fat[rb] = FAT_FREE;
                if (nxt == FAT_EOF || nxt == FAT_FREE) break;
                rb = nxt;
            }
            this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));
            return -1;
        }
    }

    // Write file data to allocated blocks
    int remaining = (int)fileInfo.size;
    int offset = 0;
    int writeBlock = fileInfo.first_blk;
    uint8_t blockBuffer[BLOCK_SIZE];

    while (writeBlock != FAT_EOF && remaining > 0) {
        int dataSize = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
        memset(blockBuffer, 0, BLOCK_SIZE);
        memcpy(blockBuffer, &fileData[offset], dataSize);

        this->disk.write(writeBlock, blockBuffer);

        offset += dataSize;
        remaining -= dataSize;
        writeBlock = this->fat[writeBlock];
    }

    // update arrays
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));


    return 0;
}

int FS::cat(string filepath) {

    // Locate the file in the root directory
    dir_entry temp[ROOT_DIR_SIZE];
    disk.read(current_directory_block, reinterpret_cast<uint8_t*>(temp));
    dir_entry fileInfo;

    int fileIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(root_dir[i].file_name, filepath.c_str()) == 0) {
            fileIndex = i;
            fileInfo = root_dir[fileIndex];
            if ((root_dir[fileIndex].access_rights & READ) == 0) {
                cout << "File not readable" << endl;
                return -1;
            }
            break;
        }
        else
        {
            if (strcmp(temp[i].file_name, filepath.c_str()) == 0) {
            fileIndex = i;
            fileInfo = temp[fileIndex];
            if ((temp[fileIndex].access_rights & READ) == 0) {
                cout << "File not readable" << endl;
                return -1;
                }
            break;
            }
        }
    }

    if (fileIndex == -1) {
        cerr << "Error: File not found.\n";
        return -1; // File not found
    }

    if (fileInfo.type != TYPE_FILE) {
        cerr << "Error: Specified path is not a file.\n";
        return -1;
    }

    // Read the file data block by block using FAT
    uint8_t buffer[BLOCK_SIZE];
    int currentBlock = fileInfo.first_blk;
    int remainingSize = fileInfo.size;

    while (currentBlock != FAT_EOF && remainingSize > 0) {
        // Read the current block from disk
        disk.read(currentBlock, buffer);

        int dataSize = (remainingSize > BLOCK_SIZE) ? BLOCK_SIZE : remainingSize;

        for (int i = 0; i < dataSize; i++) {
            cout << static_cast<char>(buffer[i]);
        }

        remainingSize -= dataSize;
        currentBlock = fat[currentBlock];
    }

    cout << endl;

    return 0;
}

int FS::ls() {

    dir_entry entries[ROOT_DIR_SIZE];
    this->disk.read(this->currentBlock, reinterpret_cast<uint8_t*>(entries));

    bool readPermission = false;

    if (this->currentBlock == ROOT_BLOCK) {
        // Root directory is always readable
        readPermission = true;
    } else {
        int parentBlock = -1;
        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (strcmp(entries[i].file_name, "..") == 0 && entries[i].type == TYPE_DIR) {
                parentBlock = entries[i].first_blk;
                break;
            }
        }

        if (parentBlock == -1) {
            cerr << "[ERROR] Could not find the parent directory.\n";
            return -1;
        }

        // Read the parent directory
        dir_entry parentDir[ROOT_DIR_SIZE];
        this->disk.read(parentBlock, reinterpret_cast<uint8_t*>(parentDir));

        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (parentDir[i].type == TYPE_DIR && parentDir[i].first_blk == (uint16_t)this->currentBlock) {
                if ((parentDir[i].access_rights & READ) != 0) {
                    readPermission = true;
                }
                break;
            }
        }
    }

    if (!readPermission) {
        cerr << "[ERROR] Current directory does not have read access.\n";
        return -1;
    }

    // Header
    cout << "name\t\ttype\t\tsize\t\taccess\n";


    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (entries[i].file_name[0] != '\0') {
            string typeStr = (entries[i].type == TYPE_DIR) ? "dir" : "file";
            string sizeStr = (entries[i].type == TYPE_DIR) ? "-" : to_string(entries[i].size);

            // Access rights string
            char r = (entries[i].access_rights & READ) ? 'r' : '-';
            char w = (entries[i].access_rights & WRITE) ? 'w' : '-';
            char x = (entries[i].access_rights & EXECUTE) ? 'x' : '-';
            string accessStr;
            accessStr.push_back(r);
            accessStr.push_back(w);
            accessStr.push_back(x);

            cout << entries[i].file_name << "\t\t" 
                      << typeStr << "\t\t" 
                      << sizeStr << "\t\t" 
                      << accessStr << "\n";
        }
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(string sourcepath, string destpath) {

    auto separatePath = [&](const string &fullPath) {
        string directoryPath;
        string filename = fullPath;
        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash != string::npos) {
            directoryPath = fullPath.substr(0, lastSlash);
            filename = fullPath.substr(lastSlash + 1);
            if (directoryPath.empty()) {
                directoryPath = "/";
            }
        } else {
            directoryPath = "";
        }
        return make_pair(directoryPath, filename);
    };

    auto [sourceDirPath, sourceFilename] = separatePath(sourcepath);
    int sourceDirBlock = resolvePathToDirectory(sourceDirPath);
    if (sourceDirBlock == -1) {
        cerr << "[ERROR] cp failed: source directory path could not be resolved.\n";
        return -1;
    }

    dir_entry sourceDir[ROOT_DIR_SIZE];
    this->disk.read(sourceDirBlock, reinterpret_cast<uint8_t*>(sourceDir));

    int sourceIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(sourceDir[i].file_name, sourceFilename.c_str()) == 0 && sourceDir[i].file_name[0] != '\0') {
            sourceIndex = i;
            break;
        }
    }
    if (sourceIndex == -1) {
        cerr << "[ERROR] Source file '" << sourceFilename << "' does not exist.\n";
        return -1;
    }

    dir_entry sourceFileInfo = sourceDir[sourceIndex];
    if (sourceFileInfo.type == TYPE_DIR) {
        cerr << "[ERROR] Source is a directory, not a file.\n";
        return -1;
    }

    vector<uint8_t> fileData;
    fileData.reserve(sourceFileInfo.size);
    int remaining = (int)sourceFileInfo.size;
    int readBlock = sourceFileInfo.first_blk;
    uint8_t blockBuffer[BLOCK_SIZE];

    while (readBlock != FAT_EOF && remaining > 0) {
        this->disk.read(readBlock, blockBuffer);
        int dataSize = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
        for (int i = 0; i < dataSize; i++) {
            fileData.push_back(blockBuffer[i]);
        }
        remaining -= dataSize;
        readBlock = this->fat[readBlock];
    }


    auto [destDirPath, destFilename] = separatePath(destpath);
    int destDirBlock = resolvePathToDirectory(destDirPath);
    if (destDirBlock == -1) {
        cerr << "[ERROR] cp failed: destination directory path could not be resolved.\n";
        return -1;
    }

    dir_entry destDirEntries[ROOT_DIR_SIZE];
    this->disk.read(destDirBlock, reinterpret_cast<uint8_t*>(destDirEntries));

    int destIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(destDirEntries[i].file_name, destFilename.c_str()) == 0 && destDirEntries[i].file_name[0] != '\0') {
            destIndex = i;
            break;
        }
    }

    if (destFilename.empty()) {
        destFilename = sourceFilename;
    }


    // Check access rights 
    if ((sourceDir[sourceIndex].access_rights & READ) == 0 || (destDirEntries[destIndex].access_rights & WRITE) == 0) {
        cerr << "[ERROR] Access right issue" << endl;
        return -1;
    }

    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(destDirEntries[i].file_name, destFilename.c_str()) == 0 && destDirEntries[i].file_name[0] != '\0') {
            if (destDirEntries[i].type == TYPE_DIR) {
                int newDirBlock = destDirEntries[i].first_blk;
                this->disk.read(newDirBlock, reinterpret_cast<uint8_t*>(destDirEntries));
                destDirBlock = newDirBlock;
                destFilename = sourceFilename;
            } else {
                cerr << "[ERROR] Destination file '" << destFilename << "' already exists (no overwrite allowed).\n";
                return -1;
            }
            break;
        }
    }

    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(destDirEntries[i].file_name, destFilename.c_str()) == 0 && destDirEntries[i].file_name[0] != '\0') {
            cerr << "[ERROR] Destination file '" << destFilename << "' already exists.\n";
            return -1;
        }
    }

    dir_entry newFile;
    memset(&newFile, 0, sizeof(dir_entry));
    if (destFilename.length() > sizeof(newFile.file_name) - 1) {
        cerr << "[ERROR] Destination filename too long.\n";
        return -1;
    }
    strncpy(newFile.file_name, destFilename.c_str(), sizeof(newFile.file_name) - 1);
    newFile.size = (uint32_t)fileData.size();
    newFile.type = TYPE_FILE;
    newFile.access_rights = READ | WRITE;

    // Allocate FAT blocks for new file
    const int fat_entries = BLOCK_SIZE / 2;
    int blocksNeeded = (newFile.size == 0) ? 1 : (int)((newFile.size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    int startBlockIndex = -1;
    for (int i = 2; i < fat_entries; i++) {
        if (this->fat[i] == FAT_FREE) {
            startBlockIndex = i;
            break;
        }
    }
    if (startBlockIndex == -1) {
        cerr << "[ERROR] No free blocks available for copying.\n";
        return -1;
    }

    newFile.first_blk = (uint16_t)startBlockIndex;

    int currentFatBlock = startBlockIndex;
    for (int j = 0; j < blocksNeeded; j++) {
        if (j == blocksNeeded - 1) {
            this->fat[currentFatBlock] = FAT_EOF;
        } else {
            int nextFreeBlock = -1;
            for (int k = currentFatBlock + 1; k < fat_entries; k++) {
                if (this->fat[k] == FAT_FREE) {
                    nextFreeBlock = k;
                    break;
                }
            }
            if (nextFreeBlock == -1) {
                cerr << "[ERROR] Not enough blocks available to copy the large file.\n";
                // Rollback
                int rb = startBlockIndex;
                while (rb != FAT_EOF && rb != FAT_FREE) {
                    int nxt = this->fat[rb];
                    this->fat[rb] = FAT_FREE;
                    if (nxt == FAT_EOF || nxt == FAT_FREE) break;
                    rb = nxt;
                }
                return -1;
            }
            this->fat[currentFatBlock] = nextFreeBlock;
            currentFatBlock = nextFreeBlock;
        }
    }

    this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));

    uint8_t dirBuffer[BLOCK_SIZE];
    bool inserted = false;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (destDirEntries[i].file_name[0] == '\0' && destDirEntries[i].first_blk == 0) {
            destDirEntries[i] = newFile;
            memcpy(dirBuffer, destDirEntries, sizeof(destDirEntries));
            this->disk.write(destDirBlock, dirBuffer);
            inserted = true;
            break;
        }
    }
    if (!inserted) {
        cerr << "[ERROR] No space in destination directory.\n";
        int rb = newFile.first_blk;
        while (rb != FAT_EOF && rb != FAT_FREE) {
            int nxt = this->fat[rb];
            this->fat[rb] = FAT_FREE;
            if (nxt == FAT_EOF || nxt == FAT_FREE) break;
            rb = nxt;
        }
        this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));
        return -1;
    }

    int toWrite = (int)newFile.size;
    int offset = 0;
    int writeBlock = newFile.first_blk;

    while (writeBlock != FAT_EOF && toWrite > 0) {
        int dataSize = (toWrite > BLOCK_SIZE) ? BLOCK_SIZE : toWrite;
        memset(blockBuffer, 0, BLOCK_SIZE);
        memcpy(blockBuffer, &fileData[offset], dataSize);

        this->disk.write(writeBlock, blockBuffer);

        offset += dataSize;
        toWrite -= dataSize;
        writeBlock = this->fat[writeBlock];
    }

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(string sourcepath, string destpath)
{
 
    auto separatePath = [&](const string &fullPath) {
        string directoryPath;
        string filename = fullPath;
        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash != string::npos) {
            directoryPath = fullPath.substr(0, lastSlash);
            filename = fullPath.substr(lastSlash + 1);
            if (directoryPath.empty()) {
                directoryPath = "/";
            }
        } else {
            directoryPath = "";
        }
        return make_pair(directoryPath, filename);
    };

    auto [sourceDirPath, sourceFilename] = separatePath(sourcepath);
    int sourceDirBlock = resolvePathToDirectory(sourceDirPath);
    if (sourceDirBlock == -1) {
        cerr << "[ERROR] mv failed: source directory path could not be resolved.\n";
        return -1;
    }

    dir_entry sourceDir[ROOT_DIR_SIZE];
    this->disk.read(sourceDirBlock, reinterpret_cast<uint8_t*>(sourceDir));

    int sourceIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(sourceDir[i].file_name, sourceFilename.c_str()) == 0 && sourceDir[i].file_name[0] != '\0') {
            sourceIndex = i;
            break;
        }
    }
    if (sourceIndex == -1) {
        cerr << "[ERROR] Source file '" << sourceFilename << "' does not exist.\n";
        return -1;
    }

    dir_entry sourceFileInfo = sourceDir[sourceIndex];
    if (sourceFileInfo.type == TYPE_DIR) {
        cerr << "[ERROR] Source is a directory, not a file.\n";
        return -1;
    }

    auto [destDirPath, destFilename] = separatePath(destpath);
    int destDirBlock = resolvePathToDirectory(destDirPath);
    if (destDirBlock == -1) {
        cerr << "[ERROR] mv failed: destination directory path could not be resolved.\n";
        return -1;
    }

    // Load the destination directory
    dir_entry destDirEntries[ROOT_DIR_SIZE];
    this->disk.read(destDirBlock, reinterpret_cast<uint8_t*>(destDirEntries));

    int destIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(destDirEntries[i].file_name, destFilename.c_str()) == 0 && destDirEntries[i].file_name[0] != '\0') {
            destIndex = i;
            break;
        }
    }

    if (destFilename.empty()) {
        destFilename = sourceFilename;
    }

    // Check access rights 
    if ((sourceDir[sourceIndex].access_rights & READ) == 0 || (destDirEntries[destIndex].access_rights & WRITE) == 0) {
        cerr << "[ERROR] Access right issue" << endl;
        return -1;
    }

    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(destDirEntries[i].file_name, destFilename.c_str()) == 0 && destDirEntries[i].type == TYPE_DIR && destDirEntries[i].file_name[0] != '\0') {
            // Destination is a directory, move into it
            destDirBlock = destDirEntries[i].first_blk;
            this->disk.read(destDirBlock, reinterpret_cast<uint8_t*>(destDirEntries));
            destFilename = sourceFilename; 
            break;
        }
    }

    // Check if the destination filename already exists
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(destDirEntries[i].file_name, destFilename.c_str()) == 0 && destDirEntries[i].file_name[0] != '\0') {
            if (destDirEntries[i].type != TYPE_DIR) {
                cerr << "[ERROR] Destination file '" << destFilename << "' already exists. No overwrite allowed.\n";
                return -1;
            }
        }
    }

    uint8_t dirBuffer[BLOCK_SIZE];

    if (sourceDirBlock == destDirBlock) {
        strncpy(sourceDir[sourceIndex].file_name, destFilename.c_str(), sizeof(sourceDir[sourceIndex].file_name) - 1);
        memcpy(dirBuffer, sourceDir, sizeof(sourceDir));
        this->disk.write(sourceDirBlock, dirBuffer);
    } else {

        // Find a free slot in the destination directory
        int freeIndex = -1;
        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (destDirEntries[i].file_name[0] == '\0' && destDirEntries[i].first_blk == 0) {
                freeIndex = i;
                break;
            }
        }
        if (freeIndex == -1) {
            cerr << "[ERROR] No space in destination directory.\n";
            return -1;
        }

        dir_entry newEntry = sourceFileInfo;
        memset(newEntry.file_name, 0, sizeof(newEntry.file_name));
        strncpy(newEntry.file_name, destFilename.c_str(), sizeof(newEntry.file_name)-1);
        destDirEntries[freeIndex] = newEntry;

        memcpy(dirBuffer, destDirEntries, sizeof(destDirEntries));
        this->disk.write(destDirBlock, dirBuffer);

        sourceDir[sourceIndex].file_name[0] = '\0';
        sourceDir[sourceIndex].first_blk = 0;
        memcpy(dirBuffer, sourceDir, sizeof(sourceDir));
        this->disk.write(sourceDirBlock, dirBuffer);
    }

    return 0;
}


// rm <filepath> removes / deletes the file <filepath>
int FS::rm(string filepath)
{


    auto separatePath = [&](const string &fullPath) {
        string directoryPath;
        string filename = fullPath;
        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash != string::npos) {
            directoryPath = fullPath.substr(0, lastSlash);
            filename = fullPath.substr(lastSlash + 1);
            if (directoryPath.empty()) {
                directoryPath = "/";
            }
        } else {
            directoryPath = "";
        }
        return make_pair(directoryPath, filename);
    };

    auto [directoryPath, filename] = separatePath(filepath);

    int dirBlock = resolvePathToDirectory(directoryPath);
    if (dirBlock == -1) {
        cerr << "[ERROR] rm failed: directory path could not be resolved.\n";
        return -1;
    }

    dir_entry dirEntries[ROOT_DIR_SIZE];
    this->disk.read(dirBlock, reinterpret_cast<uint8_t*>(dirEntries));

    int fileIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(dirEntries[i].file_name, filename.c_str()) == 0 && dirEntries[i].file_name[0] != '\0') {
            fileIndex = i;
            break;
        }
    }

    if (fileIndex == -1) {
        cerr << "[ERROR] '" << filename << "' not found in the specified directory.\n";
        return -1;
    }

    dir_entry &targetEntry = dirEntries[fileIndex];

    if (targetEntry.type == TYPE_FILE) {

        int currentBlock = targetEntry.first_blk;
        while (currentBlock != FAT_EOF && currentBlock != FAT_FREE) {
            int next = this->fat[currentBlock];
            this->fat[currentBlock] = FAT_FREE;
            currentBlock = next;
        }

        this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));

        memset(&targetEntry, 0, sizeof(dir_entry));

        uint8_t buffer[BLOCK_SIZE];
        memcpy(buffer, dirEntries, sizeof(dirEntries));
        this->disk.write(dirBlock, buffer);


    } else if (targetEntry.type == TYPE_DIR) {

        dir_entry targetDirEntries[ROOT_DIR_SIZE];
        this->disk.read(targetEntry.first_blk, reinterpret_cast<uint8_t*>(targetDirEntries));

        bool empty = true;
        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (targetDirEntries[i].file_name[0] != '\0') {
                if (strcmp(targetDirEntries[i].file_name, "..") != 0) {
                    empty = false;
                    break;
                }
            }
        }

        if (!empty) {
            cerr << "[ERROR] Directory '" << filename << "' is not empty.\n";
            return -1;
        }

        int dirBlockToFree = targetEntry.first_blk;
        this->fat[dirBlockToFree] = FAT_FREE;
        this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));

        // Remove directory entry from parent directory
        memset(&targetEntry, 0, sizeof(dir_entry));

        uint8_t buffer[BLOCK_SIZE];
        memcpy(buffer, dirEntries, sizeof(dirEntries));
        this->disk.write(dirBlock, buffer);

    } else {
        cerr << "[ERROR] Unknown entry type.\n";
        return -1;
    }

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(string filepath1, string filepath2)
{

    // Helper to separate a path into directory and filename
    auto separatePath = [&](const string &fullPath) {
        string directoryPath;
        string filename = fullPath;
        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash != string::npos) {
            directoryPath = fullPath.substr(0, lastSlash);
            filename = fullPath.substr(lastSlash + 1);
            if (directoryPath.empty()) {
                directoryPath = "/";
            }
        } else {
            directoryPath = "";
        }
        return make_pair(directoryPath, filename);
    };

    auto [srcDirPath, srcFilename] = separatePath(filepath1);
    int srcDirBlock = resolvePathToDirectory(srcDirPath);
    if (srcDirBlock == -1) {
        cerr << "[ERROR] append failed: source directory path could not be resolved.\n";
        return -1;
    }

    dir_entry srcDir[ROOT_DIR_SIZE];
    this->disk.read(srcDirBlock, reinterpret_cast<uint8_t*>(srcDir));

    int srcIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(srcDir[i].file_name, srcFilename.c_str()) == 0 && srcDir[i].file_name[0] != '\0') {
            srcIndex = i;
            break;
        }
    }

    if (srcIndex == -1) {
        cerr << "[ERROR] Source file '" << srcFilename << "' does not exist.\n";
        return -1;
    }

    dir_entry &srcFileInfo = srcDir[srcIndex];
    if (srcFileInfo.type != TYPE_FILE) {
        cerr << "[ERROR] Source path '" << srcFilename << "' is not a file.\n";
        return -1;
    }

    vector<uint8_t> srcData;
    srcData.reserve(srcFileInfo.size);
    {
        int remaining = (int)srcFileInfo.size;
        int readBlock = srcFileInfo.first_blk;
        uint8_t blockBuffer[BLOCK_SIZE];
        while (readBlock != FAT_EOF && remaining > 0) {
            this->disk.read(readBlock, blockBuffer);
            int dataSize = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
            for (int i = 0; i < dataSize; i++) {
                srcData.push_back(blockBuffer[i]);
            }
            remaining -= dataSize;
            readBlock = this->fat[readBlock];
        }
    }


    auto [destDirPath, destFilename] = separatePath(filepath2);
    int destDirBlock = resolvePathToDirectory(destDirPath);
    if (destDirBlock == -1) {
        cerr << "[ERROR] append failed: destination directory path could not be resolved.\n";
        return -1;
    }

    dir_entry destDir[ROOT_DIR_SIZE];
    this->disk.read(destDirBlock, reinterpret_cast<uint8_t*>(destDir));

    int destIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(destDir[i].file_name, destFilename.c_str()) == 0 && destDir[i].file_name[0] != '\0') {
            destIndex = i;
            break;
        }
    }

    if (destIndex == -1) {
        cerr << "[ERROR] Destination file '" << destFilename << "' does not exist.\n";
        return -1;
    }

    dir_entry &destFileInfo = destDir[destIndex];
    if (destFileInfo.type != TYPE_FILE) {
        cerr << "[ERROR] Destination path '" << destFilename << "' is not a file.\n";
        return -1;
    }

    if ((srcDir[srcIndex].access_rights & READ) == 0 || (destDir[destIndex].access_rights & WRITE) == 0) {
        cerr << "[ERROR] Access right issue" << endl;
        return -1;
    }


    uint32_t newSize = destFileInfo.size + (uint32_t)srcData.size();

    int lastBlock = destFileInfo.first_blk;
    int blockCount = 0;
    if (lastBlock == FAT_EOF) {
    } else {
        while (lastBlock != FAT_EOF) {
            lastBlock = this->fat[lastBlock];
            blockCount++;
        }
    }

    int currentBlocks = (destFileInfo.size == 0) ? 0 : (int)((destFileInfo.size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    int newBlocksNeeded = (newSize == 0) ? currentBlocks : (int)((newSize + BLOCK_SIZE - 1) / BLOCK_SIZE);
    int additionalBlocksNeeded = newBlocksNeeded - currentBlocks;


    int firstBlockOfDest = destFileInfo.first_blk;
    if (firstBlockOfDest == 0 && destFileInfo.size == 0 && additionalBlocksNeeded > 0) {
        int startBlockIndex = -1;
        const int fat_entries = BLOCK_SIZE / 2;
        for (int i = 2; i < fat_entries; i++) {
            if (this->fat[i] == FAT_FREE) {
                startBlockIndex = i;
                break;
            }
        }
        if (startBlockIndex == -1) {
            cerr << "[ERROR] No free blocks available for append.\n";
            return -1;
        }
        destFileInfo.first_blk = (uint16_t)startBlockIndex;
        this->fat[startBlockIndex] = FAT_EOF; 
        firstBlockOfDest = startBlockIndex;
        additionalBlocksNeeded = newBlocksNeeded - 1;
    }


    if (additionalBlocksNeeded > 0) {

        int lastUsedBlock = firstBlockOfDest;
        if (lastUsedBlock == FAT_EOF || lastUsedBlock == 0) {
            lastUsedBlock = firstBlockOfDest;
        } else {
            int cur = firstBlockOfDest;
            while (this->fat[cur] != FAT_EOF) {
                cur = this->fat[cur];
            }
            lastUsedBlock = cur;
        }

        const int fat_entries = BLOCK_SIZE / 2;
        int currentFatBlock = lastUsedBlock;
        if (currentFatBlock == 0) {
        }

        for (int j = 0; j < additionalBlocksNeeded; j++) {
            // Find next free block
            int nextFreeBlock = -1;
            for (int k = 2; k < fat_entries; k++) {
                if (this->fat[k] == FAT_FREE) {
                    nextFreeBlock = k;
                    break;
                }
            }
            if (nextFreeBlock == -1) {
                cerr << "[ERROR] Not enough blocks available for appending.\n";

                return -1;
            }

            if (currentFatBlock == FAT_EOF || currentFatBlock == 0) {
                // If no blocks were previously assigned
                destFileInfo.first_blk = (uint16_t)nextFreeBlock;
                this->fat[nextFreeBlock] = FAT_EOF;
                currentFatBlock = nextFreeBlock;
            } else {
                // Link from the lastUsedBlock
                this->fat[currentFatBlock] = nextFreeBlock;
                this->fat[nextFreeBlock] = FAT_EOF;
                currentFatBlock = nextFreeBlock;
            }
        }

        this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));
    }

    int writeBlock = destFileInfo.first_blk;
    int destFileRemaining = (int)destFileInfo.size;

    while (destFileRemaining > BLOCK_SIZE) {
        writeBlock = this->fat[writeBlock];
        destFileRemaining -= BLOCK_SIZE;
    }


    uint8_t blockBuffer[BLOCK_SIZE];
    if (destFileInfo.size > 0) {
        this->disk.read(writeBlock, blockBuffer);
    } else {
        // File was empty
        memset(blockBuffer, 0, BLOCK_SIZE);
    }

    // Append data from srcData to the end of dest file
    int offset = 0; // offset in srcData
    int remainingAppend = (int)srcData.size();

    // If there's room in the current last block (destFileRemaining < BLOCK_SIZE), fill it first
    int spaceInBlock = BLOCK_SIZE - destFileRemaining;
    int toWriteHere = (remainingAppend < spaceInBlock) ? remainingAppend : spaceInBlock;
    if (toWriteHere > 0) {
        memcpy(blockBuffer + destFileRemaining, &srcData[offset], toWriteHere);
        offset += toWriteHere;
        remainingAppend -= toWriteHere;

        // Write this block back
        this->disk.write(writeBlock, blockBuffer);
    }

    // Now if there's still data left, we continue with the next blocks
    writeBlock = this->fat[writeBlock];
    while (writeBlock != FAT_EOF && remainingAppend > 0) {
        int dataSize = (remainingAppend > BLOCK_SIZE) ? BLOCK_SIZE : remainingAppend;
        memset(blockBuffer, 0, BLOCK_SIZE);
        memcpy(blockBuffer, &srcData[offset], dataSize);
        this->disk.write(writeBlock, blockBuffer);
        offset += dataSize;
        remainingAppend -= dataSize;
        writeBlock = this->fat[writeBlock];
    }

    // Update the file size in directory
    destFileInfo.size = newSize;
    uint8_t dirBuffer[BLOCK_SIZE];
    memcpy(dirBuffer, destDir, sizeof(destDir));
    this->disk.write(destDirBlock, dirBuffer);

    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(string dirpath) {

    string directoryPath;
    string newDirName = dirpath;
    size_t lastSlash = dirpath.find_last_of('/');
    if (lastSlash != string::npos) {
        directoryPath = dirpath.substr(0, lastSlash);
        newDirName = dirpath.substr(lastSlash + 1);
        if (directoryPath.empty()) {
            directoryPath = "/";
        }
    } else {
        directoryPath = "";
    }

    int targetDirBlock = resolvePathToDirectory(directoryPath);
    if (targetDirBlock == -1) {
        cerr << "[ERROR] Failed to resolve directory path.\n";
        return -1;
    }

    // Read the target directory
    dir_entry currentDir[ROOT_DIR_SIZE];
    this->disk.read(targetDirBlock, reinterpret_cast<uint8_t*>(currentDir));

    // Check if directory already exists
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(currentDir[i].file_name, newDirName.c_str()) == 0 && currentDir[i].file_name[0] != '\0') {
            cerr << "[ERROR] Directory '" << newDirName << "' already exists in the target directory.\n";
            return -1;
        }
    }

    // Check if the directory has write permissions
    bool writePermission = false;

    if (targetDirBlock == ROOT_BLOCK) {
        writePermission = true;
    } else {
        int parentBlock = -1;
        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (strcmp(currentDir[i].file_name, "..") == 0 && currentDir[i].type == TYPE_DIR) {
                parentBlock = currentDir[i].first_blk;
                break;
            }
        }

        if (parentBlock == -1) {
            cerr << "[ERROR] Could not find the parent directory.\n";
            return -1;
        }

        // Read the parent directory
        dir_entry parentDir[ROOT_DIR_SIZE];
        this->disk.read(parentBlock, reinterpret_cast<uint8_t*>(parentDir));

        // In the parent directory, find the entry that references the current directory block
        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (parentDir[i].type == TYPE_DIR && parentDir[i].first_blk == (uint16_t)targetDirBlock) {
                // Check write permission of the current directory as stored in its parent directory entry
                if ((parentDir[i].access_rights & WRITE) != 0) {
                    writePermission = true;
                }
                break;
            }
        }
    }

    if (!writePermission) {
        cerr << "[ERROR] Current directory does not have write access.\n";
        return -1;
    }


    // Find a free block in FAT for the new directory
    const int fat_entries = BLOCK_SIZE / 2;
    int freeBlock = -1;
    for (int i = 2; i < fat_entries; i++) {
        if (this->fat[i] == FAT_FREE) {
            freeBlock = i;
            break;
        }
    }
    if (freeBlock == -1) {
        cerr << "[ERROR] No free blocks available for new directory.\n";
        return -1;
    }

    // Mark the block as EOF since it's a one block directory
    this->fat[freeBlock] = FAT_EOF;
    this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));

    // Find a free entry in the target directory for the new directory
    int freeIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (currentDir[i].file_name[0] == '\0' && currentDir[i].first_blk == 0) {
            freeIndex = i;
            break;
        }
    }
    if (freeIndex == -1) {
        cerr << "[ERROR] No space in target directory.\n";
        this->fat[freeBlock] = FAT_FREE;
        this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));
        return -1;
    }

    // Create new directory entry
    dir_entry newDirEntry;
    memset(&newDirEntry, 0, sizeof(dir_entry));
    if (newDirName.length() > sizeof(newDirEntry.file_name) - 1) {
        cerr << "[ERROR] Directory name too long.\n";
        this->fat[freeBlock] = FAT_FREE;
        this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat));
        return -1;
    }
    strncpy(newDirEntry.file_name, newDirName.c_str(), sizeof(newDirEntry.file_name) - 1);
    newDirEntry.file_name[sizeof(newDirEntry.file_name) - 1] = '\0';
    newDirEntry.size = sizeof(dir_entry) * 1; // at least one entry for '..'
    newDirEntry.first_blk = (uint16_t)freeBlock;
    newDirEntry.type = TYPE_DIR;
    newDirEntry.access_rights = READ | WRITE;

    currentDir[freeIndex] = newDirEntry;

    // Update the target directory on disk
    uint8_t dirBuffer[BLOCK_SIZE] = {0};
    memcpy(dirBuffer, currentDir, sizeof(currentDir));
    this->disk.write(targetDirBlock, dirBuffer);

    // Initialize the new directory block
    dir_entry newDirContent[ROOT_DIR_SIZE];
    memset(newDirContent, 0, sizeof(newDirContent));

    // '..' entry
    dir_entry dotDotEntry;
    memset(&dotDotEntry, 0, sizeof(dotDotEntry));
    strncpy(dotDotEntry.file_name, "..", sizeof(dotDotEntry.file_name) - 1);
    dotDotEntry.file_name[sizeof(dotDotEntry.file_name) - 1] = '\0';
    dotDotEntry.type = TYPE_DIR;
    dotDotEntry.first_blk = (uint16_t)targetDirBlock;
    dotDotEntry.access_rights = READ | WRITE;

    newDirContent[0] = dotDotEntry;

    uint8_t newDirBuffer[BLOCK_SIZE] = {0};
    memcpy(newDirBuffer, newDirContent, sizeof(newDirContent));
    this->disk.write(freeBlock, newDirBuffer);

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(string dirpath) {

    // If no dirpath provided, do nothing 
    if (dirpath.empty()) {
        return 0;
    }

    int newDirBlock = resolvePathToDirectory(dirpath);
    if (newDirBlock == -1) {
        cerr << "[ERROR] cd failed: cannot resolve path.\n";
        return -1;
    }

    // Check if the resolved block is actually a directory
    dir_entry entries[ROOT_DIR_SIZE];
    this->disk.read(newDirBlock, reinterpret_cast<uint8_t*>(entries));

    // A valid directory block should have a '..' entry or be the root
    bool validDir = false;
    if (newDirBlock == ROOT_BLOCK) {
        // Root is always valid
        validDir = true;
    } else {
        for (int i = 0; i < ROOT_DIR_SIZE; i++) {
            if (strcmp(entries[i].file_name, "..") == 0 && entries[i].type == TYPE_DIR) {
                validDir = true;
                break;
            }
        }
    }

    if (!validDir) {
        cerr << "[ERROR] The resolved path is not a valid directory.\n";
        return -1;
    }

    // Update current directory info
    this->currentBlock = newDirBlock;
    // Update the currentDir string
    if (dirpath[0] == '/') {
        // Absolute path
        if (newDirBlock == ROOT_BLOCK) {
            this->currentDir = "/";
        } else {
            this->currentDir = dirpath;
        }
    } else {
        // Relative path
        if (newDirBlock == ROOT_BLOCK) {
            this->currentDir = "/";
        } else {
            // Attempt to build a relative path from currentDir
            if (this->currentDir == "/") {
                
                auto normalizePath = [&](const string &base, const string &relPath) {
                    vector<string> tokens;

                    // tokenize base (skip empty tokens)
                    {
                        stringstream ss(base);
                        string t;
                        while (getline(ss, t, '/')) {
                            if (!t.empty()) tokens.push_back(t);
                        }
                    }

                    // tokenize relPath
                    {
                        stringstream ss(relPath);
                        string t;
                        while (getline(ss, t, '/')) {
                            if (t.empty() || t == ".") {
                                // skip
                                continue;
                            }
                            if (t == "..") {
                                if (!tokens.empty()) tokens.pop_back();
                            } else {
                                tokens.push_back(t);
                            }
                        }
                    }

                    // rebuild path
                    string newPath = "/";
                    for (size_t i = 0; i < tokens.size(); i++) {
                        newPath += tokens[i];
                        if (i < tokens.size() - 1) newPath += "/";
                    }
                    if (tokens.empty()) {
                        newPath = "/";
                    }
                    return newPath;
                };

                this->currentDir = normalizePath(this->currentDir, dirpath);
            } else {
                // We are in some directory other than root
                // Normalize as above
                auto normalizePath = [&](const string &base, const string &relPath) {
                    vector<string> tokens;
                    {
                        stringstream ss(base);
                        string t;
                        while (getline(ss, t, '/')) {
                            if (!t.empty()) tokens.push_back(t);
                        }
                    }
                    {
                        stringstream ss(relPath);
                        string t;
                        while (getline(ss, t, '/')) {
                            if (t.empty() || t == ".") {
                                continue;
                            }
                            if (t == "..") {
                                if (!tokens.empty()) tokens.pop_back();
                            } else {
                                tokens.push_back(t);
                            }
                        }
                    }

                    string newPath = "/";
                    for (size_t i = 0; i < tokens.size(); i++) {
                        newPath += tokens[i];
                        if (i < tokens.size() - 1) newPath += "/";
                    }
                    if (tokens.empty()) {
                        newPath = "/";
                    }
                    return newPath;
                };
                this->currentDir = normalizePath(this->currentDir, dirpath);
            }
        }
    }

    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    cout << this->currentDir << endl;
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(string accessrights, string filepath)
{
    // Helper to separate path into directory and filename
    auto separatePath = [&](const string &fullPath) {
        string directoryPath;
        string filename = fullPath;
        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash != string::npos) {
            directoryPath = fullPath.substr(0, lastSlash);
            filename = fullPath.substr(lastSlash + 1);
            if (directoryPath.empty()) {
                directoryPath = "/";
            }
        } else {
            directoryPath = "";
        }
        return make_pair(directoryPath, filename);
    };

    auto [dirPath, filename] = separatePath(filepath);
    int dirBlock = resolvePathToDirectory(dirPath);
    if (dirBlock == -1) {
        cerr << "[ERROR] chmod failed: directory path could not be resolved.\n";
        return -1;
    }

    dir_entry dirEntries[ROOT_DIR_SIZE];
    this->disk.read(dirBlock, reinterpret_cast<uint8_t*>(dirEntries));

    int fileIndex = -1;
    for (int i = 0; i < ROOT_DIR_SIZE; i++) {
        if (strcmp(dirEntries[i].file_name, filename.c_str()) == 0 && dirEntries[i].file_name[0] != '\0') {
            fileIndex = i;
            break;
        }
    }

    if (fileIndex == -1) {
        cerr << "[ERROR] File '" << filename << "' not found.\n";
        return -1;
    }

    dir_entry &targetEntry = dirEntries[fileIndex];

    // Parse accessrights string
    uint8_t newRights = 0;

    for (char i : accessrights) {
        if (i == '7') {
            newRights |= READ | WRITE | EXECUTE;
        } else if (i == '6') {
            newRights |= READ | WRITE;
        } else if (i == '5') {
            newRights |= READ | EXECUTE;
        } else if (i == '4') {
            newRights |= READ;
        } else if (i == '3') {
            newRights |= WRITE | EXECUTE;
        } else if (i == '2') {
            newRights |= WRITE;
        } else if (i == '1') {
            newRights |= EXECUTE;
        } else {
            cerr << "[ERROR] Invalid access rights number: '" << i << "'. Valid are 7, 6, 5, 4, 3, 2, 1 for rwx combinations.\n";
            return -1;
        }
    }


    targetEntry.access_rights = newRights;

    // Write updated directory entries to disk
    uint8_t buffer[BLOCK_SIZE];
    memcpy(buffer, dirEntries, sizeof(dirEntries));
    this->disk.write(dirBlock, buffer);

    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}
