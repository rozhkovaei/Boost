#include <filesystem>

#include <iostream>
#include <fstream>

#include <boost/program_options.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/regex.hpp>

#include <boost/uuid/detail/md5.hpp>
#include <boost/crc.hpp>
#include <boost/algorithm/hex.hpp>

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>

namespace fs = std::filesystem;
namespace po = boost::program_options;

const std::string MD5_HASH = "md5";
const std::string CRC32_HASH = "crc32";

class Hash
{
public:
    
    static std::string GetHash( const std::string& hash_type, const std::string buff )
    {
        std::string result = "";
        
        if( hash_type == MD5_HASH )
        {
            boost::uuids::detail::md5 hash;
            boost::uuids::detail::md5::digest_type digest;
            hash.process_bytes( buff.data(), buff.size() );
            hash.get_digest( digest );
            result = toString( digest );
        }
        else if( hash_type == CRC32_HASH )
        {
            boost::crc_32_type result;
            result.process_bytes( buff.data(), buff.length() );
            
            std::stringstream ss;
            ss << result.checksum();
            result = ss.str();
        }
        
        return result;
    }
    
    static bool IsSupportedHashType( const std::string& hash_type )
    {
        return hash_type == MD5_HASH || hash_type == CRC32_HASH;
    }
    
private:
    
    static std::string toString( const boost::uuids::detail::md5::digest_type &digest )
    {
        const auto intDigest = reinterpret_cast<const int*>( &digest );
        std::string result;
        boost::algorithm::hex( intDigest, intDigest + ( sizeof( boost::uuids::detail::md5::digest_type ) / sizeof( int ) ), std::back_inserter( result ) );
        return result;
    }
};

class FileInfo
{
public:
    FileInfo( const std::string& filename, size_t file_size )
    : mFileName( filename )
    , mFileSize( file_size )
    {}
    
    size_t GetFileSize()
    {
        return mFileSize;
    }
    
    bool GetBlock( int block_number, size_t block_size, std::string& hash, std::string& block )
    {
        ReadByBlock( block_size, hash );
        
        if( static_cast< int >( mHashBlocks.size() ) < block_number )
            return false;
        
        block = mHashBlocks[ block_number ];
        
        return true;
    }
    
private:
    
    void ReadByBlock( size_t block_size, std::string& hash )
    {
        if( mReadComplete )
            return;
        
        std::ifstream is( mFileName, std::ios::binary );

        if ( is.is_open() )
        {
            std::string buff( block_size,'0' );

            is.seekg( mReadPos, is.beg );
            
            is.read( &buff[0], block_size );
            
            mReadPos += is.gcount();
            
            if( mReadPos >= mFileSize )
                mReadComplete = true;
            
            mHashBlocks.push_back( Hash::GetHash( hash, buff ) );
        }
    }
    
    std::string mFileName;
    bool mReadComplete = 0;
    size_t mFileSize = 0;
    size_t mReadPos = 0;
    std::vector<std::string> mHashBlocks;
};

class FilesMatcher
{
    public: FilesMatcher( size_t block_size, std::string&& hash )
        : mBlockSize( block_size )
        , mHash( std::move( hash ) )
    {}
    
    bool CheckFilesEqual( FileInfo& file_first, FileInfo& file_second )
    {
        if( file_first.GetFileSize() != file_second.GetFileSize() )
        {
            return false;
        }
        
        int blocks_count = ceil( ( file_first.GetFileSize() * 1.0 ) / mBlockSize );
        
        for( int i = 0; i < blocks_count; ++i )
        {
            std::string block1, block2;
            
            if( !( file_first.GetBlock( i, mBlockSize, mHash, block1 ) & file_second.GetBlock( i, mBlockSize, mHash, block2 ) ) || ( block1 != block2 ) )
            {
                return false;
            }
        }
        
     //   std::cout << "files equal : " << files_hash_map[file1].filename << " and " << files_hash_map[file2].filename << std::endl;
        
        return true;
    }
    
private:

    size_t mBlockSize;
    std::string mHash;
};

bool CheckFileMasks( const std::string& filename, const std::vector< std::string >& masks )
{
    if( masks.empty() )
    {
        return true;
    }
    
    for( const auto& mask : masks )
    {
        boost::regex regex_mask( mask );
        boost::smatch what;
        if( boost::regex_match( filename, what, regex_mask ) )
        {
            return true;
        }
    }
    
    return false;
}

std::list< std::pair< std::string, size_t > > CollectFiles( const std::vector<std::string>& include_dirs, const std::vector<std::string>& exclude_dirs, const std::vector< std::string >& masks, size_t min_size, int scan_level )
{
    std::list< std::pair< std::string, size_t > > paths;
    
    for( const auto& dir : include_dirs )
    {
        if ( fs::exists(dir) && fs::is_directory(dir) &&
            std::find(exclude_dirs.begin(), exclude_dirs.end(), dir) == exclude_dirs.end() )
        {
            std::filesystem::recursive_directory_iterator end;

            for( std::filesystem::recursive_directory_iterator dirpos( dir ); dirpos != end; ++dirpos )
            {
                if( ( dirpos->is_directory() && !scan_level )
                   || ( std::find(exclude_dirs.begin(), exclude_dirs.end(), dirpos->path() ) != exclude_dirs.end() ) )
                {
                    dirpos.pop();
                    if( dirpos == end )
                        break;
                    
                    continue;
                }
                
                if( dirpos->is_regular_file() && dirpos->file_size() > min_size && CheckFileMasks( dirpos->path().filename().string(), masks ) )
                {
                    paths.push_back( { dirpos->path(), dirpos->file_size() } );
                }
            }
        }
    }
    
    return paths;
}

void main( int argc, const char *argv[] ) {
    
    int scan_level = 0;
    size_t file_size = 0;
    size_t block_size = 0;
    std::string hash = "";
    
    std::vector< std::string > include_dirs;
    std::vector< std::string > exclude_dirs;
    std::vector< std::string > masks;
    
    try {
        po::options_description desc{ "Options" };
        desc.add_options()
                ( "help,h", "Program options" )
                ( "include-dir,I", po::value< std::vector< std::string > >( &include_dirs )->multitoken(), "directories for scan" )
                ( "exclude-dir,E", po::value< std::vector< std::string > >( &exclude_dirs )->multitoken(), "directories excluded from scan" )
                ( "scan-level,L", po::value< int >( &scan_level )->default_value(0), "scan level : 1 - all directories, 0 - without subdirs" )
                ( "file-size,F", po::value< size_t >( &file_size )->default_value(1), "min file size (bytes)" )
                ( "mask,M", po::value< std::vector< std::string > >( &masks )->multitoken(), "file name mask" )
                ( "block-size,S", po::value< size_t >( &block_size )->default_value(5), "reading block size (bytes)" )
                ( "hash,H", po::value< std::string >( &hash )->default_value("md5"), "hash algorithm (md5 or crc32)" );
        
        po::variables_map vm;
        po::store(parse_command_line( argc, argv, desc ), vm );
        po::notify( vm );

        if ( vm.count( "help" ) )
            std::cout << desc << '\n';
        
        if ( vm.count( "include-dir" ) )
        {
            std::cout << "Include directories are: " << std::endl;
            for( const auto& dir : include_dirs )
                std::cout << dir << std::endl;
        }

        if ( vm.count("exclude-dir" ) )
        {
            std::cout << "Excluded directories are: " << std::endl;
            for( const auto& dir : exclude_dirs )
                std::cout << dir << std::endl;
        }

        if ( vm.count( "mask" ) )
        {
            std::cout << "File name masks are: " << std::endl;
            for( const auto& mask : masks )
                std::cout << mask << std::endl;
        }
        
        std::cout << "Scan level is " << scan_level << std::endl;
        std::cout << "Min file size to scan is " << file_size << std::endl;
        std::cout << "Reading block size is " << block_size << std::endl;
        std::cout << "Using hash function " << hash << std::endl;
        
    }
    catch ( const std::exception &e ) {
        std::cerr << e.what() << std::endl;
    }
    
    if( include_dirs.empty() )
        return;
    
    if( !Hash::IsSupportedHashType( hash ) )
    {
        std::cout << "Not supported hash type" << std::endl;
        return;
    }
    
    std::list< std::pair< std::string, size_t > > all_matching_files = CollectFiles( include_dirs, exclude_dirs, masks, file_size, scan_level );
    
    std::map< std::string, FileInfo > files_hash_map;
    
    if( all_matching_files.size() < 2 )
    {
        std::cout << "Nothing to match" << std::endl;
        return;
    }
    
    FilesMatcher files_matcher( block_size, std::move( hash ) );
    
    struct file {};
    struct duplicate {};

     boost::bimap<
        boost::bimaps::multiset_of<
            boost::bimaps::tagged<std::string, file>
        >,
        boost::bimaps::tagged<
            std::string,
            duplicate
        >
    > dictionary;
    
    auto it = all_matching_files.begin();
    
    while( it != all_matching_files.end() )
    {
        FileInfo file_first( it->first, it->second );
        
        auto it_next = std::next(it, 1);
        
        while( it_next != all_matching_files.end() )
        {
            if( it->first == it_next->first )
            {
                it_next = std::next( it_next, 1 );
                all_matching_files.erase( it_next );
            }
            
            FileInfo file_second( it_next->first, it_next->second );
            
           if( files_matcher.CheckFilesEqual( file_first, file_second ) )
           {
               dictionary.insert( { it->first, it_next->first } );
               
               auto tmp = std::next( it_next, 1 );
               all_matching_files.erase( it_next );
               it_next = tmp;
           }
           else
               ++it_next;
        }
        ++it;
    }
    
    it = all_matching_files.begin();
    
    std::cout << std::endl;
    
    while( it != all_matching_files.end() )
    {
        auto dict = dictionary.by<file>().find(it->first);
        if(dict == dictionary.by<file>().end())
        {
            ++it;
            continue;
        }
        
        std::cout << dict->first << std::endl;
        
        for( auto k : boost::make_iterator_range( dictionary.by<file>().equal_range( it->first ) ) )
            std::cout << k.second << std::endl;
        
        std::cout << std::endl;
        
        ++it;
    }
    
    return;
}
