#include "ConstructModSources.h"

#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

bool ConstructModSources::Decode(const TArray<uint8>& Bytes, FString& Content)
{
    if (Bytes.IsEmpty())
    {
        Content.Reset();
        return true;
    }
    const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    Content = FString(Decoded.Length(), Decoded.Get());
    const FTCHARToUTF8 Encoded(*Content, Content.Len());
    return Encoded.Length() == Bytes.Num() && (Bytes.IsEmpty() || FMemory::Memcmp(Encoded.Get(), Bytes.GetData(), Bytes.Num()) == 0);
}

FString ConstructModSources::Sha1(const FString& Content)
{
    const FTCHARToUTF8 Utf8(*Content, Content.Len());
    FSHAHash Hash;
    FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash.Hash);
    return Hash.ToString().ToLower();
}

bool ConstructModSources::Read(const FString& Directory, TArray<FCrowdyModFile>& Files, FString& Error)
{
    Files.Reset();
    Error.Reset();
    if (!IFileManager::Get().FileExists(*FPaths::Combine(Directory, TEXT("Cargo.toml"))))
    {
        Error = TEXT("This starter's source is not installed.");
        return false;
    }
    TArray<FString> Paths;
    Paths.Add(FPaths::Combine(Directory, TEXT("Cargo.toml")));
    const FString Readme = FPaths::Combine(Directory, TEXT("README.md"));
    if (IFileManager::Get().FileExists(*Readme))
    {
        Paths.Add(Readme);
    }
    TArray<FString> RustPaths;
    IFileManager::Get().FindFilesRecursive(RustPaths, *FPaths::Combine(Directory, TEXT("src")), TEXT("*.rs"), true, false);
    Paths.Append(RustPaths);
    Paths.Sort();
    FString Root = FPaths::ConvertRelativePathToFull(Directory);
    FPaths::NormalizeDirectoryName(Root);
    Root += TEXT("/");
    for (const FString& Path : Paths)
    {
        TArray<uint8> Bytes;
        FCrowdyModFile File;
        File.Path = FPaths::ConvertRelativePathToFull(Path);
        if (!FPaths::MakePathRelativeTo(File.Path, *Root))
        {
            Error = TEXT("A starter file has no relative path.");
            Files.Reset();
            return false;
        }
        File.Path.ReplaceInline(TEXT("\\"), TEXT("/"));
        if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) || !Decode(Bytes, File.Content))
        {
            Error = FString::Printf(TEXT("Could not read %s as UTF-8."), *File.Path);
            Files.Reset();
            return false;
        }
        Files.Add(MoveTemp(File));
    }
    return true;
}