/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "updater.h"

#include "base/platform/win/base_windows_safe_library.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <set>
#include <utility>
#include <vector>
#include <wincrypt.h>

bool _debug = false;

wstring updaterName, updaterDir, updateTo, exeName, customWorkingDir, customKeyFile, expectedStageHash;

bool equal(const wstring &a, const wstring &b) {
	return !_wcsicmp(a.c_str(), b.c_str());
}

void updateError(const WCHAR *msg, DWORD errorCode) {
	WCHAR errMsg[2048];
	LPWSTR errorTextFormatted = nullptr;
	auto formatFlags = FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_IGNORE_INSERTS;
	FormatMessage(
		formatFlags,
		NULL,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&errorTextFormatted,
		0,
		0);
	auto errorText = errorTextFormatted
		? errorTextFormatted
		: L"(Unknown error)";
	wsprintf(errMsg, L"%s, error code: %d\nError message: %s", msg, errorCode, errorText);

	MessageBox(0, errMsg, L"Update error!", MB_ICONERROR);

	LocalFree(errorTextFormatted);
}

HANDLE _logFile = 0;
void openLog() {
	if (!_debug || _logFile) return;
	wstring logPath = L"DebugLogs";
	if (!CreateDirectory(logPath.c_str(), NULL)) {
		DWORD errorCode = GetLastError();
		if (errorCode && errorCode != ERROR_ALREADY_EXISTS) {
			updateError(L"Failed to create log directory", errorCode);
			return;
		}
	}

	SYSTEMTIME stLocalTime;

	GetLocalTime(&stLocalTime);

	static const int maxFileLen = MAX_PATH * 10;
	WCHAR logName[maxFileLen];
	wsprintf(logName, L"DebugLogs\\%04d%02d%02d_%02d%02d%02d_upd.txt",
		stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
		stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond);
	_logFile = CreateFile(logName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (_logFile == INVALID_HANDLE_VALUE) { // :(
		updateError(L"Failed to create log file", GetLastError());
		_logFile = 0;
		return;
	}
}

void closeLog() {
	if (!_logFile) return;

	CloseHandle(_logFile);
	_logFile = 0;
}

void writeLog(const wstring &msg) {
	if (!_logFile) return;

	wstring full = msg + L'\n';
	DWORD written = 0;
	BOOL result = WriteFile(_logFile, full.c_str(), full.size() * sizeof(wchar_t), &written, 0);
	if (!result) {
		updateError((L"Failed to write log entry '" + msg + L"'").c_str(), GetLastError());
		closeLog();
		return;
	}
	BOOL flushr = FlushFileBuffers(_logFile);
	if (!flushr) {
		updateError((L"Failed to flush log on entry '" + msg + L"'").c_str(), GetLastError());
		closeLog();
		return;
	}
}

void fullClearPath(const wstring &dir) {
	WCHAR path[4096];
	memcpy(path, dir.c_str(), (dir.size() + 1) * sizeof(WCHAR));
	path[dir.size() + 1] = 0;
	writeLog(L"Fully clearing path '" + dir + L"'..");
	SHFILEOPSTRUCT file_op = {
		NULL,
		FO_DELETE,
		path,
		L"",
		FOF_NOCONFIRMATION |
		FOF_NOERRORUI |
		FOF_SILENT,
		false,
		0,
		L""
	};
	int res = SHFileOperation(&file_op);
	if (res) writeLog(L"Error: failed to clear path! :(");
}

void delFolder() {
	wstring delPathOld = L"tupdates\\ready", delPath = L"tupdates\\temp", delFolder = L"tupdates";
	fullClearPath(delPathOld);
	fullClearPath(delPath);
	RemoveDirectory(delFolder.c_str());
}

DWORD versionNum = 0, versionLen = 0, readLen = 0;
WCHAR versionStr[32] = { 0 };

namespace {

constexpr auto kStageManifestMagic = "AGST";
constexpr auto kStageManifestFormat = uint32_t(1);
constexpr auto kStageHashSize = size_t(32);
constexpr auto kMaxStageManifestSize = size_t(32 * 1024);
constexpr auto kMaxStagePathSize = uint32_t(240);
constexpr auto kMaxStageFilesCount = uint32_t(32);
const WCHAR *kApplicationExe = L"Allowgram.exe";
const WCHAR *kUpdaterExe = L"AllowgramUpdater.exe";

struct StageFile {
	wstring path;
	uint64_t size = 0;
	std::array<BYTE, 32> sha256 = {};
};

struct StageManifest {
	uint64_t version = 0;
	wstring displayVersion;
	std::vector<StageFile> files;
};

[[nodiscard]] wstring lower(wstring value) {
	for (auto &ch : value) {
		ch = WCHAR(towlower(ch));
	}
	return value;
}

[[nodiscard]] bool validHexSha256(const wstring &value) {
	if (value.size() != 64) {
		return false;
	}
	for (const auto ch : value) {
		if (!((ch >= L'0' && ch <= L'9')
			|| (ch >= L'a' && ch <= L'f')
			|| (ch >= L'A' && ch <= L'F'))) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] BYTE hexNibble(WCHAR ch) {
	return (ch >= L'0' && ch <= L'9')
		? BYTE(ch - L'0')
		: (ch >= L'a' && ch <= L'f')
		? BYTE(ch - L'a' + 10)
		: BYTE(ch - L'A' + 10);
}

[[nodiscard]] std::array<BYTE, 32> hexToHash(const wstring &value) {
	auto result = std::array<BYTE, 32>();
	for (auto i = size_t(0); i != result.size(); ++i) {
		result[i] = BYTE((hexNibble(value[2 * i]) << 4)
			| hexNibble(value[2 * i + 1]));
	}
	return result;
}

[[nodiscard]] bool readWholeFile(
		const wstring &path,
		std::vector<BYTE> *content,
		size_t maxSize) {
	const auto file = CreateFile(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		0);
	if (file == INVALID_HANDLE_VALUE) {
		writeLog(L"Error: could not open '" + path + L"'");
		return false;
	}
	auto size = LARGE_INTEGER();
	if (!GetFileSizeEx(file, &size)
		|| size.QuadPart < 0
		|| uint64_t(size.QuadPart) > maxSize) {
		writeLog(L"Error: bad file size for '" + path + L"'");
		CloseHandle(file);
		return false;
	}
	content->resize(size_t(size.QuadPart));
	DWORD read = 0;
	const auto ok = content->empty()
		|| (ReadFile(file, content->data(), DWORD(content->size()), &read, 0)
			&& read == DWORD(content->size()));
	CloseHandle(file);
	if (!ok) {
		writeLog(L"Error: could not read '" + path + L"'");
		return false;
	}
	return true;
}

[[nodiscard]] bool sha256Data(
		const BYTE *data,
		size_t size,
		std::array<BYTE, 32> *result) {
	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	if (!CryptAcquireContextW(
			&provider,
			nullptr,
			nullptr,
			PROV_RSA_AES,
			CRYPT_VERIFYCONTEXT)
		|| !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
		if (hash) {
			CryptDestroyHash(hash);
		}
		if (provider) {
			CryptReleaseContext(provider, 0);
		}
		return false;
	}
	while (size) {
		const auto chunk = std::min<size_t>(size, 1024 * 1024);
		if (!CryptHashData(hash, data, DWORD(chunk), 0)) {
			CryptDestroyHash(hash);
			CryptReleaseContext(provider, 0);
			return false;
		}
		data += chunk;
		size -= chunk;
	}
	DWORD length = DWORD(result->size());
	const auto ok = CryptGetHashParam(
		hash,
		HP_HASHVAL,
		result->data(),
		&length,
		0) && length == DWORD(result->size());
	CryptDestroyHash(hash);
	CryptReleaseContext(provider, 0);
	return ok;
}

[[nodiscard]] bool sha256File(
		const wstring &path,
		uint64_t expectedSize,
		std::array<BYTE, 32> *result) {
	const auto file = CreateFile(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		0);
	if (file == INVALID_HANDLE_VALUE) {
		writeLog(L"Error: could not open '" + path + L"' for hashing");
		return false;
	}
	auto size = LARGE_INTEGER();
	if (!GetFileSizeEx(file, &size)
		|| size.QuadPart < 0
		|| uint64_t(size.QuadPart) != expectedSize) {
		writeLog(L"Error: staged file size changed: '" + path + L"'");
		CloseHandle(file);
		return false;
	}
	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	if (!CryptAcquireContextW(
			&provider,
			nullptr,
			nullptr,
			PROV_RSA_AES,
			CRYPT_VERIFYCONTEXT)
		|| !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
		writeLog(L"Error: could not initialize SHA-256");
		if (hash) {
			CryptDestroyHash(hash);
		}
		if (provider) {
			CryptReleaseContext(provider, 0);
		}
		CloseHandle(file);
		return false;
	}
	auto buffer = std::array<BYTE, 1024 * 1024>();
	auto ok = true;
	while (ok) {
		DWORD read = 0;
		if (!ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, 0)) {
			writeLog(L"Error: could not hash '" + path + L"'");
			ok = false;
			break;
		}
		if (!read) {
			break;
		}
		if (!CryptHashData(hash, buffer.data(), read, 0)) {
			writeLog(L"Error: could not update SHA-256");
			ok = false;
			break;
		}
	}
	DWORD length = DWORD(result->size());
	ok = ok
		&& CryptGetHashParam(hash, HP_HASHVAL, result->data(), &length, 0)
		&& length == DWORD(result->size());
	CryptDestroyHash(hash);
	CryptReleaseContext(provider, 0);
	CloseHandle(file);
	return ok;
}

[[nodiscard]] bool readBytes(
		const std::vector<BYTE> &data,
		size_t *offset,
		void *to,
		size_t count) {
	if (count > data.size() - *offset) {
		return false;
	}
	memcpy(to, data.data() + *offset, count);
	*offset += count;
	return true;
}

[[nodiscard]] bool readU32(
		const std::vector<BYTE> &data,
		size_t *offset,
		uint32_t *value) {
	auto bytes = std::array<BYTE, 4>();
	if (!readBytes(data, offset, bytes.data(), bytes.size())) {
		return false;
	}
	*value = uint32_t(bytes[0])
		| (uint32_t(bytes[1]) << 8)
		| (uint32_t(bytes[2]) << 16)
		| (uint32_t(bytes[3]) << 24);
	return true;
}

[[nodiscard]] bool readU64(
		const std::vector<BYTE> &data,
		size_t *offset,
		uint64_t *value) {
	uint32_t low = 0;
	uint32_t high = 0;
	if (!readU32(data, offset, &low) || !readU32(data, offset, &high)) {
		return false;
	}
	*value = uint64_t(low) | (uint64_t(high) << 32);
	return true;
}

[[nodiscard]] bool readText(
		const std::vector<BYTE> &data,
		size_t *offset,
		uint32_t size,
		wstring *value) {
	if (!size || size > kMaxStagePathSize || size > data.size() - *offset) {
		return false;
	}
	value->clear();
	for (auto i = uint32_t(0); i != size; ++i) {
		const auto ch = data[*offset + i];
		if (ch < 0x20 || ch > 0x7E) {
			return false;
		}
		value->push_back(WCHAR(ch));
	}
	*offset += size;
	return true;
}

[[nodiscard]] bool normalizeRelativePath(wstring value, wstring *normalized) {
	for (auto &ch : value) {
		if (ch == L'/') {
			ch = L'\\';
		}
	}
	if (value.empty()
		|| value[0] == L'\\'
		|| value.find(L':') != wstring::npos
		|| value.rfind(L"\\\\", 0) == 0) {
		return false;
	}
	auto result = wstring();
	auto start = size_t(0);
	while (start <= value.size()) {
		const auto slash = value.find(L'\\', start);
		const auto count = (slash == wstring::npos)
			? value.size() - start
			: slash - start;
		const auto part = value.substr(start, count);
		if (part.empty() || part == L"." || part == L"..") {
			return false;
		}
		if (!result.empty()) {
			result += L'\\';
		}
		result += part;
		if (slash == wstring::npos) {
			break;
		}
		start = slash + 1;
	}
	*normalized = result;
	return true;
}

[[nodiscard]] bool payloadFileAllowed(const wstring &path) {
	static const auto Allowed = std::set<wstring>{
		L"allowgram.exe",
		L"allowgramupdater.exe",
		L"build-info.json",
		L"legal",
		L"license",
		L"readme.txt",
	};
	return Allowed.count(lower(path)) != 0;
}

[[nodiscard]] bool hasReparsePoint(const wstring &path) {
	const auto attributes = GetFileAttributes(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

[[nodiscard]] bool parseStageManifestBytes(
		const std::vector<BYTE> &bytes,
		StageManifest *manifest) {
	if (bytes.size() < 4 || bytes.size() > kMaxStageManifestSize) {
		writeLog(L"Error: bad stage manifest size");
		return false;
	}
	auto offset = size_t(0);
	char magic[4] = {};
	uint32_t format = 0;
	uint32_t displaySize = 0;
	uint32_t filesCount = 0;
	if (!readBytes(bytes, &offset, magic, sizeof(magic))
		|| memcmp(magic, kStageManifestMagic, sizeof(magic))
		|| !readU32(bytes, &offset, &format)
		|| format != kStageManifestFormat
		|| !readU64(bytes, &offset, &manifest->version)
		|| !manifest->version
		|| !readU32(bytes, &offset, &displaySize)
		|| !readText(bytes, &offset, displaySize, &manifest->displayVersion)) {
		writeLog(L"Error: bad stage manifest header");
		return false;
	}
	auto packageHash = std::array<BYTE, 32>();
	if (!readBytes(bytes, &offset, packageHash.data(), packageHash.size())
		|| !readU32(bytes, &offset, &filesCount)
		|| !filesCount
		|| filesCount > kMaxStageFilesCount) {
		writeLog(L"Error: bad stage manifest file count");
		return false;
	}
	auto seen = std::set<wstring>();
	manifest->files.clear();
	manifest->files.reserve(filesCount);
	for (auto i = uint32_t(0); i != filesCount; ++i) {
		uint32_t pathSize = 0;
		StageFile file;
		if (!readU32(bytes, &offset, &pathSize)
			|| !readText(bytes, &offset, pathSize, &file.path)
			|| !readU64(bytes, &offset, &file.size)
			|| !readBytes(
				bytes,
				&offset,
				file.sha256.data(),
				file.sha256.size())) {
			writeLog(L"Error: bad stage manifest entry");
			return false;
		}
		auto normalized = wstring();
		if (!normalizeRelativePath(file.path, &normalized)
			|| normalized != file.path
			|| !payloadFileAllowed(file.path)) {
			writeLog(L"Error: stage manifest path is not allowed: " + file.path);
			return false;
		}
		const auto folded = lower(file.path);
		if (!seen.insert(folded).second) {
			writeLog(L"Error: duplicate stage manifest path: " + file.path);
			return false;
		}
		manifest->files.push_back(std::move(file));
	}
	if (offset != bytes.size()) {
		writeLog(L"Error: trailing stage manifest bytes");
		return false;
	}
	for (const auto &required : { L"allowgram.exe", L"allowgramupdater.exe", L"build-info.json" }) {
		if (seen.count(required) == 0) {
			writeLog(wstring(L"Error: required staged file is missing: ") + required);
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool validateNoUnexpectedFiles(
		const wstring &updDir,
		const std::set<wstring> &allowedFiles) {
	const auto validateTData = [&] {
		const auto tdata = updDir + L"\\tdata";
		WIN32_FIND_DATA findData;
		const auto handle = FindFirstFileEx(
			(tdata + L"\\*").c_str(),
			FindExInfoStandard,
			&findData,
			FindExSearchNameMatch,
			0,
			0);
		if (handle == INVALID_HANDLE_VALUE) {
			writeLog(L"Error: missing staged tdata directory");
			return false;
		}
		auto ok = true;
		do {
			const auto name = wstring(findData.cFileName);
			if (name == L"." || name == L"..") {
				continue;
			}
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
				writeLog(L"Error: staged tdata contains a reparse point");
				ok = false;
				break;
			}
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				writeLog(L"Error: staged tdata contains a directory");
				ok = false;
				break;
			}
			const auto folded = lower(name);
			if (folded != L"version"
				&& folded != L"stage-manifest.bin"
				&& folded != L"package.tdup") {
				writeLog(L"Error: staged tdata contains unexpected file: " + name);
				ok = false;
				break;
			}
		} while (FindNextFile(handle, &findData));
		const auto error = GetLastError();
		FindClose(handle);
		return ok && (!error || error == ERROR_NO_MORE_FILES);
	};

	WIN32_FIND_DATA findData;
	const auto handle = FindFirstFileEx(
		(updDir + L"\\*").c_str(),
		FindExInfoStandard,
		&findData,
		FindExSearchNameMatch,
		0,
		0);
	if (handle == INVALID_HANDLE_VALUE) {
		writeLog(L"Error: missing staged update directory");
		return false;
	}
	auto sawTData = false;
	auto ok = true;
	do {
		const auto name = wstring(findData.cFileName);
		if (name == L"." || name == L"..") {
			continue;
		}
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
			writeLog(L"Error: staged update contains a reparse point");
			ok = false;
			break;
		}
		const auto folded = lower(name);
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (folded != L"tdata") {
				writeLog(L"Error: staged update contains unexpected directory: " + name);
				ok = false;
				break;
			}
			sawTData = true;
			continue;
		}
		if (folded != L"ready" && allowedFiles.count(folded) == 0) {
			writeLog(L"Error: staged update contains unexpected file: " + name);
			ok = false;
			break;
		}
	} while (FindNextFile(handle, &findData));
	const auto error = GetLastError();
	FindClose(handle);
	return ok && (!error || error == ERROR_NO_MORE_FILES)
		&& sawTData
		&& validateTData();
}

[[nodiscard]] bool validateStageManifest(
		const wstring &updDir,
		StageManifest *manifest) {
	if (!validHexSha256(expectedStageHash)) {
		writeLog(L"Error: missing or bad -stagehash");
		return false;
	}
	if (hasReparsePoint(updDir) || hasReparsePoint(updDir + L"\\tdata")) {
		writeLog(L"Error: staged update directory is a reparse point");
		return false;
	}
	std::vector<BYTE> bytes;
	if (!readWholeFile(
			updDir + L"\\tdata\\stage-manifest.bin",
			&bytes,
			kMaxStageManifestSize)) {
		return false;
	}
	auto actualHash = std::array<BYTE, 32>();
	if (!sha256Data(bytes.data(), bytes.size(), &actualHash)
		|| actualHash != hexToHash(expectedStageHash)) {
		writeLog(L"Error: stage manifest hash changed");
		return false;
	}
	if (!parseStageManifestBytes(bytes, manifest)) {
		return false;
	}
	auto allowedFiles = std::set<wstring>();
	for (const auto &file : manifest->files) {
		allowedFiles.insert(lower(file.path));
		const auto path = updDir + L"\\" + file.path;
		if (hasReparsePoint(path)) {
			writeLog(L"Error: staged file is a reparse point: " + file.path);
			return false;
		}
		auto hash = std::array<BYTE, 32>();
		if (!sha256File(path, file.size, &hash) || hash != file.sha256) {
			writeLog(L"Error: staged file hash changed: " + file.path);
			return false;
		}
	}
	return validateNoUnexpectedFiles(updDir, allowedFiles);
}

[[nodiscard]] bool copyVerifiedFile(
		const wstring &from,
		const wstring &to,
		const StageFile &file) {
	if (equal(to, updaterName)) {
		auto currentHash = std::array<BYTE, 32>();
		if (!sha256File(to, file.size, &currentHash)
			|| currentHash != file.sha256) {
			writeLog(L"Error: current helper does not match staged helper");
			return false;
		}
		writeLog(L"Current helper already matches staged helper.");
		return true;
	}
	const auto temporary = to + L".allowgram-new";
	DeleteFile(temporary.c_str());
	BOOL copyResult = FALSE;
	for (auto tries = 0; tries != 100; ++tries) {
		copyResult = CopyFile(from.c_str(), temporary.c_str(), FALSE);
		if (copyResult) {
			break;
		}
		Sleep(100);
	}
	if (!copyResult) {
		writeLog(L"Error: failed to copy staged file to temporary target: " + to);
		DeleteFile(temporary.c_str());
		return false;
	}
	auto copiedHash = std::array<BYTE, 32>();
	if (!sha256File(temporary, file.size, &copiedHash)
		|| copiedHash != file.sha256) {
		writeLog(L"Error: copied temporary hash mismatch: " + to);
		DeleteFile(temporary.c_str());
		return false;
	}
	if (!MoveFileEx(
			temporary.c_str(),
			to.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
		writeLog(L"Error: failed to move temporary file into place: " + to);
		DeleteFile(temporary.c_str());
		return false;
	}
	return true;
}

void setVersionString(const wstring &value) {
	versionNum = 1;
	versionLen = DWORD(std::min<size_t>(value.size(), 32 - 1)
		* sizeof(WCHAR));
	memcpy(versionStr, value.c_str(), versionLen);
	versionStr[versionLen / sizeof(WCHAR)] = 0;
}

} // namespace

bool update() {
	writeLog(L"Update started..");

	const auto updDir = wstring(L"tupdates\\temp");
	const auto readyFilePath = wstring(L"tupdates\\temp\\ready");
	if (GetFileAttributes(L"tupdates\\ready") != INVALID_FILE_ATTRIBUTES) {
		writeLog(L"Error: legacy tupdates\\ready tree is not accepted.");
		delFolder();
		return false;
	}
	const auto readyFile = CreateFile(
		readyFilePath.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		0);
	if (readyFile == INVALID_HANDLE_VALUE) {
		return true;
	}
	CloseHandle(readyFile);

	StageManifest manifest;
	if (!validateStageManifest(updDir, &manifest)) {
		delFolder();
		return false;
	}
	setVersionString(manifest.displayVersion);

	if (!equal(exeName, kApplicationExe)) {
		writeLog(L"Error: Allowgram updates require Allowgram.exe, got " + exeName);
		delFolder();
		return false;
	}

	for (const auto &file : manifest.files) {
		const auto from = updDir + L"\\" + file.path;
		const auto to = updateTo + file.path;
		writeLog(L"Copying verified file '" + from + L"' to '" + to + L"'..");
		if (!copyVerifiedFile(from, to, file)) {
			delFolder();
			return false;
		}
	}

	writeLog(L"Update succeed! Clearing folder..");
	delFolder();
	return true;
}

void updateRegistry() {
	if (versionNum && versionNum != 0x7FFFFFFF && versionNum != 0x7FFFFFFE) {
		writeLog(L"Updating registry..");
		versionStr[versionLen / 2] = 0;
		HKEY rkey;
		LSTATUS status = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{9CF76959-9AB3-4893-8B08-45CBA5B248C4}_is1", 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &rkey);
		if (status == ERROR_SUCCESS) {
			writeLog(L"Checking registry install location..");
			static const int bufSize = 4096;
			DWORD locationType, locationSize = bufSize * 2;
			WCHAR locationStr[bufSize], exp[bufSize];
			if (RegQueryValueEx(rkey, L"InstallLocation", 0, &locationType, (BYTE*)locationStr, &locationSize) == ERROR_SUCCESS) {
				locationSize /= 2;
				if (locationStr[locationSize - 1]) {
					locationStr[locationSize++] = 0;
				}
				if (locationType == REG_EXPAND_SZ) {
					DWORD copy = ExpandEnvironmentStrings(locationStr, exp, bufSize);
					if (copy <= bufSize) {
						memcpy(locationStr, exp, copy * sizeof(WCHAR));
					}
				}
				if (locationType == REG_EXPAND_SZ || locationType == REG_SZ) {
					if (PathCanonicalize(exp, locationStr)) {
						memcpy(locationStr, exp, bufSize * sizeof(WCHAR));
						if (GetFullPathName(L".", bufSize, exp, 0) < bufSize) {
							wstring installpath = locationStr, mypath = exp;
							if (!mypath.empty() && mypath.back() != L'\\') {
                                mypath += L'\\';
                                }
                                if (equal(installpath, mypath)) {
								WCHAR nameStr[bufSize], dateStr[bufSize], publisherStr[bufSize], icongroupStr[bufSize];
								SYSTEMTIME stLocalTime;
								GetLocalTime(&stLocalTime);
								RegSetValueEx(rkey, L"DisplayVersion", 0, REG_SZ, (const BYTE*)versionStr, ((versionLen / 2) + 1) * sizeof(WCHAR));
								wsprintf(nameStr, L"Allowgram");
								RegSetValueEx(rkey, L"DisplayName", 0, REG_SZ, (const BYTE*)nameStr, (wcslen(nameStr) + 1) * sizeof(WCHAR));
								wsprintf(publisherStr, L"Allowgram contributors");
								RegSetValueEx(rkey, L"Publisher", 0, REG_SZ, (const BYTE*)publisherStr, (wcslen(publisherStr) + 1) * sizeof(WCHAR));
								wsprintf(icongroupStr, L"Allowgram");
								RegSetValueEx(rkey, L"Inno Setup: Icon Group", 0, REG_SZ, (const BYTE*)icongroupStr, (wcslen(icongroupStr) + 1) * sizeof(WCHAR));
								wsprintf(dateStr, L"%04d%02d%02d", stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay);
								RegSetValueEx(rkey, L"InstallDate", 0, REG_SZ, (const BYTE*)dateStr, (wcslen(dateStr) + 1) * sizeof(WCHAR));

								const WCHAR *appURL = L"https://github.com/molotovgit/allowgram";
								RegSetValueEx(rkey, L"HelpLink", 0, REG_SZ, (const BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
								RegSetValueEx(rkey, L"URLInfoAbout", 0, REG_SZ, (const BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
								RegSetValueEx(rkey, L"URLUpdateInfo", 0, REG_SZ, (const BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
							}
						}
					}
				}
			}
			RegCloseKey(rkey);
		}
	}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE prevInstance, LPWSTR cmdParamarg, int cmdShow) {
	base::Platform::InitDynamicLibraries();

	openLog();

	_oldWndExceptionFilter = SetUnhandledExceptionFilter(_exceptionFilter);
//	CAPIHook apiHook("kernel32.dll", "SetUnhandledExceptionFilter", (PROC)RedirectedSetUnhandledExceptionFilter);

	writeLog(L"Updaters started..");

	LPWSTR *args;
	int argsCount;

	bool needupdate = false, autostart = false, debug = false, writeprotected = false, startintray = false;
	args = CommandLineToArgvW(GetCommandLine(), &argsCount);
	if (args) {
		for (int i = 1; i < argsCount; ++i) {
			writeLog(std::wstring(L"Argument: ") + args[i]);
			if (equal(args[i], L"-update")) {
				needupdate = true;
			} else if (equal(args[i], L"-autostart")) {
				autostart = true;
			} else if (equal(args[i], L"-debug")) {
				debug = _debug = true;
				openLog();
			} else if (equal(args[i], L"-startintray")) {
				startintray = true;
			} else if (equal(args[i], L"-writeprotected") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				writeprotected = true;
				updateTo = args[i];
				for (int j = 0, l = updateTo.size(); j < l; ++j) {
					if (updateTo[j] == L'/') {
						updateTo[j] = L'\\';
					}
				}
			} else if (equal(args[i], L"-workdir") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				customWorkingDir = args[i];
			} else if (equal(args[i], L"-key") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				customKeyFile = args[i];
			} else if (equal(args[i], L"-stagehash") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				expectedStageHash = args[i];
			} else if (equal(args[i], L"-exename") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				exeName = args[i];
				for (int j = 0, l = exeName.size(); j < l; ++j) {
					if (exeName[j] == L'/' || exeName[j] == L'\\') {
						exeName = kApplicationExe;
						break;
					}
				}
			}
		}
		if (exeName.empty()) {
			exeName = kApplicationExe;
		}
		if (needupdate) writeLog(L"Need to update!");
		if (autostart) writeLog(L"From autostart!");
		if (writeprotected) writeLog(L"Write Protected folder!");
		if (!customWorkingDir.empty()) writeLog(L"Will pass custom working dir: " + customWorkingDir);
		if (!expectedStageHash.empty()) writeLog(L"Expected stage manifest hash is: " + expectedStageHash);

		updaterName = args[0];
		writeLog(L"Updater name is: " + updaterName);
		const auto updaterExeLength = wcslen(kUpdaterExe);
		if (updaterName.size() >= updaterExeLength) {
			if (equal(updaterName.substr(updaterName.size() - updaterExeLength), kUpdaterExe)) {
				updaterDir = updaterName.substr(0, updaterName.size() - updaterExeLength);
				writeLog(L"Updater dir is: " + updaterDir);
				if (!writeprotected) {
					updateTo = updaterDir;
				}
				if (!updateTo.empty() && updateTo.back() != L'\\') {
					updateTo += L'\\';
				}
				writeLog(L"Update to: " + updateTo);
				if (needupdate && update()) {
					updateRegistry();
				}
				if (writeprotected) {
					if (DeleteFile(L"tupdates\\temp\\tdata\\version")) {
						writeLog(L"Version file deleted!");
					} else {
						writeLog(L"Error: could not delete version file");
					}
				}
			} else {
				writeLog(L"Error: bad exe name!");
			}
		} else {
			writeLog(L"Error: short exe name!");
		}
		LocalFree(args);
	} else {
		writeLog(L"Error: No command line arguments!");
	}

	wstring targs;
	if (autostart) targs += L" -autostart";
	if (debug) targs += L" -debug";
	if (startintray) targs += L" -startintray";
	if (!customWorkingDir.empty()) {
		targs += L" -workdir \"" + customWorkingDir + L"\"";
	}
	if (!customKeyFile.empty()) {
		targs += L" -key \"" + customKeyFile + L"\"";
	}
	writeLog(L"Result arguments: " + targs);

	bool executed = false;
	if (writeprotected) {
		writeLog(L"Trying to run un-elevated by temp.lnk");

		HRESULT hres = CoInitialize(0);
		if (SUCCEEDED(hres)) {
			IShellLink* psl;
			HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
			if (SUCCEEDED(hres)) {
				IPersistFile* ppf;

				wstring exe = updateTo + exeName, dir = updateTo;
				psl->SetArguments((targs.size() ? targs.substr(1) : targs).c_str());
				psl->SetPath(exe.c_str());
				psl->SetWorkingDirectory(dir.c_str());
				psl->SetDescription(L"");

				hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

				if (SUCCEEDED(hres)) {
					wstring lnk = L"tupdates\\temp\\temp.lnk";
					hres = ppf->Save(lnk.c_str(), TRUE);
					ppf->Release();

					if (SUCCEEDED(hres)) {
						writeLog(L"Executing un-elevated through link..");
						ShellExecute(0, 0, L"explorer.exe", lnk.c_str(), 0, SW_SHOWNORMAL);
						executed = true;
					} else {
						writeLog(L"Error: ppf->Save failed");
					}
				} else {
					writeLog(L"Error: Could not create interface IID_IPersistFile");
				}
				psl->Release();
			} else {
				writeLog(L"Error: could not create instance of IID_IShellLink");
			}
			CoUninitialize();
		} else {
			writeLog(L"Error: Could not initialize COM");
		}
	}
	if (!executed) {
		ShellExecute(0, 0, (updateTo + exeName).c_str(), (L"-noupdate" + targs).c_str(), 0, SW_SHOWNORMAL);
	}

	writeLog(L"Executed '" + exeName + L"', closing log and quitting..");
	closeLog();

	return 0;
}

static const WCHAR *_programName = L"Allowgram"; // folder in APPDATA, if current path is unavailable for writing
static const WCHAR *_exeName = L"AllowgramUpdater.exe";

LPTOP_LEVEL_EXCEPTION_FILTER _oldWndExceptionFilter = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_miniDumpWriteDump)(
	_In_ HANDLE hProcess,
	_In_ DWORD ProcessId,
	_In_ HANDLE hFile,
	_In_ MINIDUMP_TYPE DumpType,
	_In_opt_ PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	_In_opt_ PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	_In_opt_ PMINIDUMP_CALLBACK_INFORMATION CallbackParam
);
t_miniDumpWriteDump miniDumpWriteDump = 0;

HANDLE _generateDumpFileAtPath(const WCHAR *path) {
	static const int maxFileLen = MAX_PATH * 10;

	WCHAR szPath[maxFileLen];
	wsprintf(szPath, L"%stdata\\", path);
	if (!CreateDirectory(szPath, NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			return 0;
		}
	}
	wsprintf(szPath, L"%sdumps\\", path);
	if (!CreateDirectory(szPath, NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			return 0;
		}
	}

	WCHAR szFileName[maxFileLen];
	WCHAR szExeName[maxFileLen];

	wcscpy_s(szExeName, _exeName);
	WCHAR *dotFrom = wcschr(szExeName, WCHAR(L'.'));
	if (dotFrom) {
		wsprintf(dotFrom, L"");
	}

	SYSTEMTIME stLocalTime;

	GetLocalTime(&stLocalTime);

	wsprintf(
		szFileName, L"%s%s-%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp",
		szPath, szExeName, updaterVersionStr,
		stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
		stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond,
		GetCurrentProcessId(), GetCurrentThreadId());
	return CreateFile(szFileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
}

void _generateDump(EXCEPTION_POINTERS* pExceptionPointers) {
	static const int maxFileLen = MAX_PATH * 10;

	closeLog();

	HMODULE hDll = LoadLibrary(L"DBGHELP.DLL");
	if (!hDll) return;

	miniDumpWriteDump = (t_miniDumpWriteDump)GetProcAddress(hDll, "MiniDumpWriteDump");
	if (!miniDumpWriteDump) return;

	HANDLE hDumpFile = 0;

	WCHAR szPath[maxFileLen];
	DWORD len = GetModuleFileName(GetModuleHandle(0), szPath, maxFileLen);
	if (!len) return;

	WCHAR *pathEnd = szPath + len;

	if (!_wcsicmp(pathEnd - wcslen(_exeName), _exeName)) {
		wsprintf(pathEnd - wcslen(_exeName), L"");
		hDumpFile = _generateDumpFileAtPath(szPath);
	}
	if (!hDumpFile || hDumpFile == INVALID_HANDLE_VALUE) {
		WCHAR wstrPath[maxFileLen];
		DWORD wstrPathLen = GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen);
		if (wstrPathLen) {
			wsprintf(wstrPath + wstrPathLen, L"\\%s\\", _programName);
			hDumpFile = _generateDumpFileAtPath(wstrPath);
		}
	}

	if (!hDumpFile || hDumpFile == INVALID_HANDLE_VALUE) {
		return;
	}

	MINIDUMP_EXCEPTION_INFORMATION ExpParam = {0};
	ExpParam.ThreadId = GetCurrentThreadId();
	ExpParam.ExceptionPointers = pExceptionPointers;
	ExpParam.ClientPointers = TRUE;

	miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithDataSegs, &ExpParam, NULL, NULL);
}

LONG CALLBACK _exceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
	_generateDump(pExceptionPointers);
	return _oldWndExceptionFilter ? (*_oldWndExceptionFilter)(pExceptionPointers) : EXCEPTION_CONTINUE_SEARCH;
}

// see http://www.codeproject.com/Articles/154686/SetUnhandledExceptionFilter-and-the-C-C-Runtime-Li
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI RedirectedSetUnhandledExceptionFilter(_In_opt_ LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) {
	// When the CRT calls SetUnhandledExceptionFilter with NULL parameter
	// our handler will not get removed.
	_oldWndExceptionFilter = lpTopLevelExceptionFilter;
	return 0;
}
