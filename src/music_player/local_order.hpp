#pragma once

#include <cwctype>
#include <filesystem>
#include <string>

inline bool naturalLocalPathLess(const std::filesystem::path &leftPath, const std::filesystem::path &rightPath)
{
	const std::wstring left = leftPath.generic_wstring();
	const std::wstring right = rightPath.generic_wstring();
	size_t leftIndex = 0, rightIndex = 0;
	while (leftIndex < left.size() && rightIndex < right.size()) {
		const wchar_t leftCharacter = left[leftIndex];
		const wchar_t rightCharacter = right[rightIndex];
		if (std::iswdigit(leftCharacter) && std::iswdigit(rightCharacter)) {
			size_t leftEnd = leftIndex, rightEnd = rightIndex;
			while (leftEnd < left.size() && std::iswdigit(left[leftEnd])) ++leftEnd;
			while (rightEnd < right.size() && std::iswdigit(right[rightEnd])) ++rightEnd;
			size_t leftNumber = leftIndex, rightNumber = rightIndex;
			while (leftNumber < leftEnd && left[leftNumber] == L'0') ++leftNumber;
			while (rightNumber < rightEnd && right[rightNumber] == L'0') ++rightNumber;
			const size_t leftDigits = leftEnd - leftNumber;
			const size_t rightDigits = rightEnd - rightNumber;
			if (leftDigits != rightDigits) return leftDigits < rightDigits;
			for (size_t offset = 0; offset < leftDigits; ++offset)
				if (left[leftNumber + offset] != right[rightNumber + offset])
					return left[leftNumber + offset] < right[rightNumber + offset];
			const size_t leftWidth = leftEnd - leftIndex;
			const size_t rightWidth = rightEnd - rightIndex;
			if (leftWidth != rightWidth) return leftWidth < rightWidth;
			leftIndex = leftEnd;
			rightIndex = rightEnd;
			continue;
		}
		const wchar_t foldedLeft = std::towlower(leftCharacter);
		const wchar_t foldedRight = std::towlower(rightCharacter);
		if (foldedLeft != foldedRight) return foldedLeft < foldedRight;
		++leftIndex;
		++rightIndex;
	}
	return left.size() < right.size();
}
