#pragma once

#include <windows.h>

#include <string>

std::string redactDiagnosticText(std::string text);
bool diagnosticLineMatchesDate(const std::string &line, const std::string &issueDate);

struct DiagnosticPackageResult {
	bool exported = false;
	bool cancelled = false;
	unsigned textFiles = 0;
	unsigned dumpFiles = 0;
	unsigned skippedFiles = 0;
	std::wstring path;
	std::string error;
};

DiagnosticPackageResult exportDiagnosticPackage(HWND owner, const std::wstring &suggestedName,
	const std::string &feedbackReport, const std::string &issueDate);
