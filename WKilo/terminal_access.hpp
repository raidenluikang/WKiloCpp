#include <cstdint> // intptr_t
#include <cstdio> // FILE

#include <string>

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

// Some code taken from - https://github.com/microsoft/terminal/issues/8820

namespace wkilocpp
{

	struct ScreenSize
	{
		int rows;
		int cols;
	};

	struct ScreenHandle
	{
	public:
		ScreenHandle();
		~ScreenHandle();

		ScreenHandle(const ScreenHandle&) = delete;
		ScreenHandle& operator = (const ScreenHandle&) = delete;

		ScreenHandle(ScreenHandle&&) noexcept;
		ScreenHandle& operator = (ScreenHandle&&) noexcept;

		void enableRawMode(void);

		int read(int ignored, char* c, int toread);

		int write(int ignored, const char* buf, size_t length);

		static ScreenSize getWindowSize();
	private:
		struct impl;
		struct impl* d_;
	};
	

	//These function do not depend ScreeHandle	
	bool writeFileUtf8(std::string_view fpath, std::string_view data);

	int winGetLastError();

} // wkilocpp