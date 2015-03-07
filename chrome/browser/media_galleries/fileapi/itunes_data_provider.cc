// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_galleries/fileapi/itunes_data_provider.h"

#include <map>

#include "base/bind.h"
#include "base/callback.h"
#include "base/format_macros.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/platform_file.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/media_galleries/fileapi/media_file_system_backend.h"
#include "chrome/browser/media_galleries/imported_media_gallery_registry.h"
#include "chrome/common/media_galleries/itunes_library.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "webkit/browser/fileapi/native_file_util.h"

using chrome::MediaFileSystemBackend;

namespace itunes {

namespace {

typedef base::Callback<void(scoped_ptr<base::FilePathWatcher> watcher)>
    FileWatchStartedCallback;

// Colon and slash are not allowed in filenames, replace them with underscore.
std::string EscapeBadCharacters(const std::string& input) {
  std::string result;
  ReplaceChars(input, ":/", "_", &result);
  return result;
}

ITunesDataProvider::Album MakeUniqueTrackNames(const parser::Album& album) {
  // TODO(vandebo): It would be nice to ensure that names returned from here
  // are stable, but aside from persisting every name returned, it's not
  // obvious how to do that (without including the track id in every name).
  typedef std::set<const parser::Track*> TrackRefs;
  typedef std::map<ITunesDataProvider::TrackName, TrackRefs> AlbumInfo;

  ITunesDataProvider::Album result;
  AlbumInfo duped_tracks;

  parser::Album::const_iterator album_it;
  for (album_it = album.begin(); album_it != album.end(); ++album_it) {
    const parser::Track& track = *album_it;
    std::string name =
        EscapeBadCharacters(track.location.BaseName().AsUTF8Unsafe());
    duped_tracks[name].insert(&track);
  }

  for (AlbumInfo::const_iterator name_it = duped_tracks.begin();
       name_it != duped_tracks.end();
       ++name_it) {
    const TrackRefs& track_refs = name_it->second;
    if (track_refs.size() == 1) {
      result[name_it->first] = (*track_refs.begin())->location;
    } else {
      for (TrackRefs::const_iterator track_it = track_refs.begin();
           track_it != track_refs.end();
           ++track_it) {
        base::FilePath track_file_name = (*track_it)->location.BaseName();
        std::string id =
            base::StringPrintf(" (%" PRId64 ")", (*track_it)->id);
        std::string uniquified_track_name =
            track_file_name.InsertBeforeExtensionASCII(id).AsUTF8Unsafe();
        std::string escaped_track_name =
            EscapeBadCharacters(uniquified_track_name);
        result[escaped_track_name] = (*track_it)->location;
      }
    }
  }

  return result;
}

// Bounces |path| and |error| to |callback| from the FILE thread to the media
// task runner.
void OnLibraryChanged(const base::FilePathWatcher::Callback& callback,
                      const base::FilePath& path,
                      bool error) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  MediaFileSystemBackend::MediaTaskRunner()->PostTask(
      FROM_HERE, base::Bind(callback, path, error));
}

// The watch has to be started on the FILE thread, and the callback called by
// the FilePathWatcher also needs to run on the FILE thread.
void StartLibraryWatchOnFileThread(
    const base::FilePath& library_path,
    const FileWatchStartedCallback& watch_started_callback,
    const base::FilePathWatcher::Callback& library_changed_callback) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  scoped_ptr<base::FilePathWatcher> watcher(new base::FilePathWatcher);
  bool success = watcher->Watch(
      library_path, false /*recursive*/,
      base::Bind(&OnLibraryChanged, library_changed_callback));
  if (!success)
    LOG(ERROR) << "Adding watch for " << library_path.value() << " failed";
  MediaFileSystemBackend::MediaTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(watch_started_callback, base::Passed(&watcher)));
}

// |result_path| is set if |locale_string| maps to a localized directory name
// and it exists in the filesystem.
bool CheckLocaleStringAutoAddPath(
    const base::FilePath& media_path,
    const std::map<std::string, std::string>& localized_dir_names,
    const std::string& locale_string,
    base::FilePath* result_path) {
  DCHECK(!media_path.empty());
  DCHECK(!localized_dir_names.empty());
  DCHECK(!locale_string.empty());
  DCHECK(result_path);

  std::map<std::string, std::string>::const_iterator it =
      localized_dir_names.find(locale_string);
  if (it == localized_dir_names.end())
    return false;

  base::FilePath localized_auto_add_path =
      media_path.Append(base::FilePath::FromUTF8Unsafe(it->second));
  if (!fileapi::NativeFileUtil::DirectoryExists(localized_auto_add_path))
    return false;

  *result_path = localized_auto_add_path;
  return true;
}

// This method is complex because Apple localizes the directory name in versions
// of iTunes before 10.6.
base::FilePath GetAutoAddPath(const base::FilePath& library_path) {
  const char kiTunesMediaDir[] = "iTunes Media";
  base::FilePath media_path =
      library_path.DirName().AppendASCII(kiTunesMediaDir);

  // Test 'universal' path first.
  base::FilePath universal_auto_add_path =
      media_path.AppendASCII("Automatically Add to iTunes.localized");
  if (fileapi::NativeFileUtil::DirectoryExists(universal_auto_add_path))
    return universal_auto_add_path;

  // Test user locale. Localized directory names encoded in UTF-8.
  std::map<std::string, std::string> localized_dir_names;
  localized_dir_names["nl"] = "Voeg automatisch toe aan iTunes";
  localized_dir_names["en"] = "Automatically Add to iTunes";
  localized_dir_names["fr"] = "Ajouter automatiquement \xC3\xA0 iTunes";
  localized_dir_names["de"] = "Automatisch zu iTunes hinzuf\xC3\xBCgen";
  localized_dir_names["it"] = "Aggiungi automaticamente a iTunes";
  localized_dir_names["ja"] = "iTunes \xE3\x81\xAB\xE8\x87\xAA\xE5\x8B\x95\xE7"
                              "\x9A\x84\xE3\x81\xAB\xE8\xBF\xBD\xE5\x8A\xA0";
  localized_dir_names["es"] = "A\xC3\xB1""adir autom\xC3\xA1ticamente a iTunes";
  localized_dir_names["da"] = "F\xC3\xB8j automatisk til iTunes";
  localized_dir_names["en-GB"] = "Automatically Add to iTunes";
  localized_dir_names["fi"] = "Lis\xC3\xA4\xC3\xA4 automaattisesti iTunesiin";
  localized_dir_names["ko"] = "iTunes\xEC\x97\x90 \xEC\x9E\x90\xEB\x8F\x99\xEC"
                              "\x9C\xBC\xEB\xA1\x9C \xEC\xB6\x94\xEA\xB0\x80";
  localized_dir_names["no"] = "Legg til automatisk i iTunes";
  localized_dir_names["pl"] = "Automatycznie dodaj do iTunes";
  localized_dir_names["pt"] = "Adicionar Automaticamente ao iTunes";
  localized_dir_names["pt-PT"] = "Adicionar ao iTunes automaticamente";
  localized_dir_names["ru"] = "\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE\xD0\xBC\xD0\xB0"
                              "\xD1\x82\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0\xBA"
                              "\xD0\xB8 \xD0\xB4\xD0\xBE\xD0\xB1\xD0\xB0\xD0"
                              "\xB2\xD0\xBB\xD1\x8F\xD1\x82\xD1\x8C \xD0\xB2"
                              "iTunes";
  localized_dir_names["sv"] = "L\xC3\xA4gg automatiskt till i iTunes";
  localized_dir_names["zh-CN"] = "\xE8\x87\xAA\xE5\x8A\xA8\xE6\xB7\xBB\xE5\x8A"
                                 "\xA0\xE5\x88\xB0 iTunes";
  localized_dir_names["zh-TW"] = "\xE8\x87\xAA\xE5\x8B\x95\xE5\x8A\xA0\xE5\x85"
                                 "\xA5 iTunes";

  const icu::Locale locale = icu::Locale::getDefault();
  const char* language = locale.getLanguage();
  const char* country = locale.getCountry();

  base::FilePath result_path;
  if (language != NULL && *language != '\0') {
    if (country != NULL && *country != '\0' &&
        CheckLocaleStringAutoAddPath(media_path, localized_dir_names,
                                     std::string(language) + "-" + country,
                                     &result_path)) {
      return result_path;
    }

    if (CheckLocaleStringAutoAddPath(media_path, localized_dir_names,
                                     language, &result_path)) {
      return result_path;
    }
  }

  // Fallback to trying English.
  if (CheckLocaleStringAutoAddPath(media_path, localized_dir_names,
                                   "en", &result_path)) {
    return result_path;
  }

  return base::FilePath();
}

}  // namespace

ITunesDataProvider::ITunesDataProvider(const base::FilePath& library_path)
    : library_path_(library_path),
      auto_add_path_(GetAutoAddPath(library_path)),
      needs_refresh_(true),
      is_valid_(false) {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  DCHECK(!library_path_.empty());

  content::BrowserThread::PostTask(
      content::BrowserThread::FILE,
      FROM_HERE,
      base::Bind(StartLibraryWatchOnFileThread,
                 library_path_,
                 base::Bind(&ITunesDataProvider::OnLibraryWatchStartedCallback),
                 base::Bind(&ITunesDataProvider::OnLibraryChangedCallback)));
}

ITunesDataProvider::~ITunesDataProvider() {}

// TODO(vandebo): add a file watch that resets |needs_refresh_| when the
// file changes.
void ITunesDataProvider::RefreshData(const ReadyCallback& ready_callback) {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  if (!needs_refresh_) {
    ready_callback.Run(is_valid_);
    return;
  }

  needs_refresh_ = false;
  xml_parser_ = new SafeITunesLibraryParser(
      library_path_,
      base::Bind(&ITunesDataProvider::OnLibraryParsedCallback, ready_callback));
  xml_parser_->Start();
}

const base::FilePath& ITunesDataProvider::library_path() const {
  return library_path_;
}

const base::FilePath& ITunesDataProvider::auto_add_path() const {
  return auto_add_path_;
}

bool ITunesDataProvider::KnownArtist(const ArtistName& artist) const {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  DCHECK(is_valid_);
  return ContainsKey(library_, artist);
}

bool ITunesDataProvider::KnownAlbum(const ArtistName& artist,
                                    const AlbumName& album) const {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  DCHECK(is_valid_);
  Library::const_iterator library_it = library_.find(artist);
  if (library_it == library_.end())
    return false;
  return ContainsKey(library_it->second, album);
}

base::FilePath ITunesDataProvider::GetTrackLocation(
    const ArtistName& artist, const AlbumName& album,
    const TrackName& track) const {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  DCHECK(is_valid_);
  Library::const_iterator library_it = library_.find(artist);
  if (library_it == library_.end())
    return base::FilePath();

  Artist::const_iterator artist_it = library_it->second.find(album);
  if (artist_it == library_it->second.end())
    return base::FilePath();

  Album::const_iterator album_it = artist_it->second.find(track);
  if (album_it == artist_it->second.end())
    return base::FilePath();
  return album_it->second;
}

std::set<ITunesDataProvider::ArtistName>
ITunesDataProvider::GetArtistNames() const {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  DCHECK(is_valid_);
  std::set<ArtistName> result;
  Library::const_iterator it;
  for (it = library_.begin(); it != library_.end(); ++it) {
    result.insert(it->first);
  }
  return result;
}

std::set<ITunesDataProvider::AlbumName> ITunesDataProvider::GetAlbumNames(
    const ArtistName& artist) const {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  DCHECK(is_valid_);
  std::set<AlbumName> result;
  Library::const_iterator artist_lookup = library_.find(artist);
  if (artist_lookup == library_.end())
    return result;

  const Artist& artist_entry = artist_lookup->second;
  Artist::const_iterator it;
  for (it = artist_entry.begin(); it != artist_entry.end(); ++it) {
    result.insert(it->first);
  }
  return result;
}

ITunesDataProvider::Album ITunesDataProvider::GetAlbum(
    const ArtistName& artist, const AlbumName& album) const {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  DCHECK(is_valid_);
  Album result;
  Library::const_iterator artist_lookup = library_.find(artist);
  if (artist_lookup != library_.end()) {
    Artist::const_iterator album_lookup = artist_lookup->second.find(album);
    if (album_lookup != artist_lookup->second.end())
      result = album_lookup->second;
  }
  return result;
}

// static
void ITunesDataProvider::OnLibraryWatchStartedCallback(
    scoped_ptr<base::FilePathWatcher> library_watcher) {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  ITunesDataProvider* provider =
      chrome::ImportedMediaGalleryRegistry::ITunesDataProvider();
  if (provider)
    provider->OnLibraryWatchStarted(library_watcher.Pass());
}

// static
void ITunesDataProvider::OnLibraryChangedCallback(const base::FilePath& path,
                                                  bool error) {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  ITunesDataProvider* provider =
      chrome::ImportedMediaGalleryRegistry::ITunesDataProvider();
  if (provider)
    provider->OnLibraryChanged(path, error);
}

// static
void ITunesDataProvider::OnLibraryParsedCallback(
    const ReadyCallback& ready_callback,
    bool result,
    const parser::Library& library) {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  ITunesDataProvider* provider =
      chrome::ImportedMediaGalleryRegistry::ITunesDataProvider();
  if (!provider) {
    ready_callback.Run(false);
    return;
  }
  provider->OnLibraryParsed(ready_callback, result, library);
}

void ITunesDataProvider::OnLibraryWatchStarted(
    scoped_ptr<base::FilePathWatcher> library_watcher) {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  library_watcher_.reset(library_watcher.release());
}

void ITunesDataProvider::OnLibraryChanged(const base::FilePath& path,
                                          bool error) {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  DCHECK_EQ(library_path_.value(), path.value());
  if (error)
    LOG(ERROR) << "Error watching " << library_path_.value();
  needs_refresh_ = true;
}

void ITunesDataProvider::OnLibraryParsed(const ReadyCallback& ready_callback,
                                         bool result,
                                         const parser::Library& library) {
  DCHECK(MediaFileSystemBackend::CurrentlyOnMediaTaskRunnerThread());
  is_valid_ = result;
  if (is_valid_) {
    library_.clear();
    for (parser::Library::const_iterator artist_it = library.begin();
         artist_it != library.end();
         ++artist_it) {
      std::string artist_name = EscapeBadCharacters(artist_it->first);
      for (parser::Albums::const_iterator album_it = artist_it->second.begin();
           album_it != artist_it->second.end();
           ++album_it) {
        std::string album_name = EscapeBadCharacters(album_it->first);
        library_[artist_name][album_name] =
            MakeUniqueTrackNames(album_it->second);
      }
    }
  }
  ready_callback.Run(is_valid_);
}

}  // namespace itunes
