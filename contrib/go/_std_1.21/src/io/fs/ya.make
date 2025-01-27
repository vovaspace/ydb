GO_LIBRARY()
IF (OS_DARWIN AND ARCH_ARM64)
    SRCS(
		format.go
		fs.go
		glob.go
		readdir.go
		readfile.go
		stat.go
		sub.go
		walk.go
    )
ELSEIF (OS_DARWIN AND ARCH_X86_64)
    SRCS(
		format.go
		fs.go
		glob.go
		readdir.go
		readfile.go
		stat.go
		sub.go
		walk.go
    )
ELSEIF (OS_LINUX AND ARCH_AARCH64)
    SRCS(
		format.go
		fs.go
		glob.go
		readdir.go
		readfile.go
		stat.go
		sub.go
		walk.go
    )
ELSEIF (OS_LINUX AND ARCH_X86_64)
    SRCS(
		format.go
		fs.go
		glob.go
		readdir.go
		readfile.go
		stat.go
		sub.go
		walk.go
    )
ELSEIF (OS_WINDOWS AND ARCH_X86_64)
    SRCS(
		format.go
		fs.go
		glob.go
		readdir.go
		readfile.go
		stat.go
		sub.go
		walk.go
    )
ENDIF()
END()
