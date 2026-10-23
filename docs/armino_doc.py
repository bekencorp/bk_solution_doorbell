#!/usr/bin/env python3

import os
import shutil
import subprocess
import sys
import argparse
import glob
import re
from typing import Match

PRINT_READ = "\033[91m"
PRINT_WARN = "\033[93m"
PRINT_RESET = "\033[0m"

def _cjk_display_width(text: str) -> int:
    """Return reST title underline width (wide/fullwidth chars count as 2)."""
    import unicodedata
    width = 0
    for ch in text:
        if unicodedata.east_asian_width(ch) in ('W', 'F'):
            width += 2
        else:
            width += 1
    return width + 4

def _text_display_width(text: str) -> int:
    """Grid table 单元格对齐宽度（CJK 等宽字符计 2）。"""
    import unicodedata
    width = 0
    for ch in text:
        if unicodedata.east_asian_width(ch) in ('W', 'F'):
            width += 2
        else:
            width += 1
    return width

class MarkdownToRST:
    """Markdown → RST 转换器"""

    def __init__(self, src_path=None, dst_path=None):
        """初始化转换器"""
        self.src_path = src_path  # 源文件路径
        self.dst_path = dst_path  # 目标文件路径

    def convert_file(self, src_file, dst_file):
        """将指定的Markdown文件转换为RST文件"""
        # 构建完整的文件路径
        src_file_path = os.path.join(self.src_path, src_file) if self.src_path else src_file
        dst_file_path = os.path.join(self.dst_path, dst_file) if self.dst_path else dst_file

        # 确保目标目录存在
        if self.dst_path:
            os.makedirs(self.dst_path, exist_ok=True)

        # 读取源文件
        with open(src_file_path, "r", encoding="utf-8") as f:
            md_text = f.read()

        # 转换内容
        rst_text = self.convert(md_text)

        # 写入目标文件
        with open(dst_file_path, "w", encoding="utf-8") as f:
            f.write(rst_text)

    def convert(self, text: str) -> str:
        """将 Markdown 文本转换为 reStructuredText (RST)"""
        # 首先清理文本，移除可能导致问题的内容
        text = self._clean_text_before_conversion(text)
        text = self._fix_rst_warning_blocks(text)
        text = self._convert_backtick_links(text)
        # 代码块须在表格转换之前，避免 ``` 内的 | 行被误判为 Markdown 表格
        text = self._convert_codeblocks(text)
        text = self._convert_markdown_tables(text)

        # 然后按行处理其他格式
        lines = text.splitlines()
        processed_lines = []
        in_codeblock = False

        for line in lines:
            stripped = line.strip()
            if stripped.startswith('.. code-block::'):
                in_codeblock = True
                processed_lines.append(line)
                continue
            if in_codeblock:
                if stripped and not line.startswith('   '):
                    in_codeblock = False
                    processed_lines.append(self._convert_markdown_line(line))
                else:
                    processed_lines.append(line)
                continue
            if self._is_grid_table_line(line):
                processed_lines.append(line)
                continue
            # 检查是否已经包含reST语法
            if self._contains_rst_syntax(line):
                processed_lines.append(line)
            else:
                processed_lines.append(self._convert_markdown_line(line))

        # 将处理后的行重新组合为文本
        text = '\n'.join(processed_lines)
        text = self._fix_nested_list_indent(text)
        text = self._ensure_blank_before_directives(text)
        return self._ensure_document_title(text)

    def _convert_markdown_line(self, line: str) -> str:
        """Markdown 行内格式转换（反引号区间内不做 emphasis 转换）。"""
        if ':link_to_translation:' in line:
            line = self._convert_link_to_translation(line)
            line = self._convert_unordered_list(line)
            return line
        if re.match(r'^\s*#{1,6}\s+', line):
            return self._convert_markdown_heading_line(line)
        line = self._normalize_md_emphasis_in_backticks(line)
        parts = line.split('`')
        for i in range(0, len(parts), 2):
            segment = parts[i]
            segment = self._convert_bold(segment)
            segment = self._convert_italic(segment)
            segment = self._convert_headings(segment)
            segment = self._convert_images(segment)
            segment = self._convert_link_to_translation(segment)
            segment = self._convert_links(segment)
            segment = self._convert_unordered_list(segment)
            segment = self._convert_ordered_list(segment)
            parts[i] = segment
        for i in range(1, len(parts), 2):
            parts[i] = re.sub(r'\*\*', '', parts[i])
        line = '`'.join(parts)
        line = self._convert_inline_code(line)
        line = self._sanitize_rst_line(line)
        return line

    def _format_rst_heading(self, level: int, title: str) -> str:
        width = _cjk_display_width(title)
        if level == 1:
            underline = "=" * width
        elif level == 2:
            underline = "-" * width
        elif level == 3:
            underline = "," * width
        elif level == 4:
            underline = "." * width
        elif level == 5:
            underline = "*" * width
        else:
            underline = "~" * width
        return f"{title}\n{underline}\n"

    def _convert_markdown_heading_line(self, line: str) -> str:
        """标题行含行内代码时，须整行转换，不能按反引号 split。"""
        line = self._normalize_md_emphasis_in_backticks(line)
        m = re.match(r'^\s*(#{1,6})\s+(.*)$', line)
        if not m:
            return line
        level = len(m.group(1))
        title = m.group(2).strip()
        title = self._convert_bold(title)
        title = self._convert_italic(title)
        title = self._convert_inline_code(title)
        title = self._sanitize_rst_line(title)
        return self._format_rst_heading(level, title)

    def _normalize_md_emphasis_in_backticks(self, line: str) -> str:
        """README 中 `` `**bold**` `` / `` `**text` `` 等混用统一为普通反引号内容。"""
        line = re.sub(r'\[`([^`]+)`\]\(([^)]+)\)', r'[\1](\2)', line)
        line = re.sub(r'`\[([^\]]+)\]\(([^)]+)\)`', r'[\1](\2)', line)
        line = re.sub(r'`\*\*(.+?)\*\*`', r'`\1`', line)
        line = re.sub(r'`\*\*([^*`]+)`', r'`\1`', line)
        line = re.sub(r'`([^*`]+)\*\*`', r'`\1`', line)
        return line

    def _sanitize_rst_line(self, line: str) -> str:
        """修正 README 转 RST 后易触发 docutils 警告的 inline markup。"""
        line = re.sub(r'\*\*(`[^`]+`)\*\*', r':strong:\1', line)
        line = re.sub(r'\*\*(``[^`]+``)\*\*', r':strong:\1', line)
        line = re.sub(r'(``[^`]+``)\*\*', r'\1', line)
        line = re.sub(r'\*\*(\.\w+)\*\*', r'``\1``', line)
        line = re.sub(r'\*\*([^*`]+_[^*`]+)\*\*', r'``\1``', line)
        # 兜底：未在反引号区间转换的 **bold**
        line = re.sub(r'\*\*([^*]+)\*\*', r':strong:`\1`', line)
        line = re.sub(r'\*\*', '', line)
        # 修正 inline code 误伤 :strong:`text` 的情况
        line = re.sub(r':strong:``([^`]+)``', r':strong:`\1`', line)
        # :strong:/literal/`` 与全角括号之间加空格，避免 docutils 解析错误
        line = re.sub(r'(:strong:`[^`]+`)（', r'\1 （', line)
        line = re.sub(r'(:literal:`[^`]+`)（', r'\1 （', line)
        line = re.sub(r'>`_（', r'>`_ （', line)
        line = re.sub(r'(``[^`]+``)（', r'\1 （', line)
        return line

    def _fix_nested_list_indent(self, text: str) -> str:
        """修正 Markdown 嵌套列表转 RST 后的缩进与空行。

        RST 要求：父级 ``-`` 与缩进子列表之间不能有空行（否则会被当成 block quote）；
        从缩进子列表回到顶层 ``-`` 时则需要空行分隔。
        """
        lines = text.splitlines()
        out = []
        for i, line in enumerate(lines):
            is_top_level_item = bool(
                re.match(r'^[-*]\s', line) or re.match(r'^\d+\.', line)
            )

            # 去掉父级列表项与缩进子项之间误插的空行
            if (
                line == ''
                and out
                and out[-1] != ''
                and not re.match(r'^  ', out[-1])
                and i + 1 < len(lines)
                and re.match(r'^  +- ', lines[i + 1])
            ):
                continue

            # 缩进子列表结束后回到顶层列表项前补空行
            if is_top_level_item and out and out[-1] != '':
                if re.match(r'^  ', out[-1]):
                    out.append('')

            out.append(line)
        return '\n'.join(out)

    def _ensure_blank_before_directives(self, text: str) -> str:
        """段落与 .. directive 之间补空行，避免 code-block 等紧贴正文。"""
        return re.sub(
            r'(\S)\n(\.\. (?:code-block|warning|note|important|caution|tip)::)',
            r'\1\n\n\2',
            text,
        )

    def _ensure_document_title(self, text: str) -> str:
        """首行非 Markdown 标题时，提升为 RST 文档标题（避免 toctree no title）。"""
        lines = text.splitlines()
        idx = 0
        while idx < len(lines) and not lines[idx].strip():
            idx += 1
        if idx >= len(lines):
            return text
        first = lines[idx].strip()
        if first.startswith('#') or first.startswith('..') or first.startswith(':link_to_translation:'):
            return text
        if idx + 1 < len(lines) and re.match(r'^[=\-~^`\'",.:*+#_]+$', lines[idx + 1].strip()):
            return text
        width = _cjk_display_width(first)
        lines.insert(idx + 1, '=' * width)
        lines.insert(idx + 2, '')
        return '\n'.join(lines)

    def _convert_markdown_tables(self, text: str) -> str:
        """将 Markdown 表格转为 RST grid table，避免 |---| 被当成替换引用。"""
        lines = text.splitlines()
        result = []
        i = 0
        while i < len(lines):
            line = lines[i]
            # 已缩进行（含 code-block 内容）不参与 Markdown 表格识别
            if re.match(r'^\s', line):
                result.append(line)
                i += 1
                continue
            if '|' in line and i + 1 < len(lines) and re.match(r'^\s*\|?[\s:\-|]+\|', lines[i + 1]):
                table_lines = [line]
                i += 1
                while i < len(lines) and '|' in lines[i]:
                    table_lines.append(lines[i])
                    i += 1
                result.append(self._markdown_table_to_grid(table_lines))
                result.append('')
                continue
            result.append(line)
            i += 1
        return '\n'.join(result)

    @staticmethod
    def _parse_md_table_row(line: str) -> list:
        line = line.strip()
        if line.startswith('|'):
            line = line[1:]
        if line.endswith('|'):
            line = line[:-1]
        return [cell.strip() for cell in line.split('|')]

    def _convert_table_cell(self, text: str) -> str:
        text = self._normalize_md_emphasis_in_backticks(text)
        text = self._convert_bold(text)
        text = self._convert_italic(text)
        text = self._convert_links(text)
        text = self._convert_inline_code(text)
        return self._sanitize_rst_line(text)

    @staticmethod
    def _format_grid_cell(text: str, col_width: int) -> str:
        pad = col_width + 1 - _text_display_width(text)
        return ' ' + text + ' ' * max(pad, 1)

    @staticmethod
    def _grid_table_border(col_widths, char='-') -> str:
        return '+' + '+'.join(char * (w + 2) for w in col_widths) + '+'

    def _grid_table_row(self, cells, col_widths) -> str:
        parts = [self._format_grid_cell(c, col_widths[i]) for i, c in enumerate(cells)]
        return '|' + '|'.join(parts) + '|'

    def _markdown_table_to_grid(self, table_lines: list) -> str:
        parsed = [self._parse_md_table_row(l) for l in table_lines]
        if len(parsed) < 2:
            return '\n'.join(table_lines)
        header = parsed[0]
        body = parsed[2:] if len(parsed) > 2 else []
        all_rows = [header] + body
        num_cols = max(len(r) for r in all_rows)
        for row in all_rows:
            while len(row) < num_cols:
                row.append('')
        converted = [[self._convert_table_cell(c) for c in row] for row in all_rows]
        col_widths = [0] * num_cols
        for row in converted:
            for i, cell in enumerate(row):
                col_widths[i] = max(col_widths[i], _text_display_width(cell))
        out = [self._grid_table_border(col_widths, '-')]
        out.append(self._grid_table_row(converted[0], col_widths))
        out.append(self._grid_table_border(col_widths, '='))
        for row in converted[1:]:
            out.append(self._grid_table_row(row, col_widths))
            out.append(self._grid_table_border(col_widths, '-'))
        return '\n'.join(out)

    @staticmethod
    def _is_grid_table_line(line: str) -> bool:
        s = line.strip()
        if not s:
            return False
        if s.startswith('+') and re.match(r'^\+[-=+]+\+$', s):
            return True
        return s.startswith('|') and s.endswith('|')

    def _fix_rst_warning_blocks(self, text: str) -> str:
        """把 .. warning:: 后面误接 markdown 围栏代码块的内容改成 RST 缩进段落。"""
        def repl(m: Match) -> str:
            body = m.group(1).strip('\n')
            out = ['.. warning::', '']
            for bl in body.splitlines():
                out.append('   ' + bl)
            return '\n'.join(out)

        return re.sub(
            r'\.\. warning::\s*\n+```\w*\n(.*?)```',
            repl,
            text,
            flags=re.DOTALL,
        )

    def _convert_backtick_links(self, text: str) -> str:
        """`[text](url)` -> `text <url>`_（README 里常见反引号包裹的链接）。"""
        return re.sub(
            r'`\[([^\]]+)\]\(([^)]+)\)`',
            r'`\1 <\2>`_',
            text,
        )

    def _convert_link_to_translation(self, text: str) -> str:
        # 确保使用正确的反引号格式，避免出现双反引号
        # 匹配无序列表中的语言链接
        text = re.sub(r'^\s*[*-]\s+\[English\]\(\.\/README\.md\)', r':link_to_translation:`en:[English]`', text, flags=re.MULTILINE)
        text = re.sub(r'^\s*[*-]\s+\[中文\]\(\.\/README_CN\.md\)', r':link_to_translation:`zh_CN:[中文]`', text, flags=re.MULTILINE)
        # 匹配普通文本中的语言链接
        text = re.sub(r'\[English\]\(\.\/README\.md\)', r':link_to_translation:`en:[English]`', text)
        text = re.sub(r'\[中文\]\(\.\/README_CN\.md\)', r':link_to_translation:`zh_CN:[中文]`', text)
        return text


    def _contains_rst_syntax(self, line: str) -> bool:
        """检查一行文本是否已经包含reST语法"""
        # 检查常见的reST语法模式
        patterns = [
            # 检查以..开头的指令（如.. image::, .. code-block::等）
            r'^\s*\.\.',
            # 检查以:开头的指令（如:link_to_translation:）
            r'^\s*:',
            r':link_to_translation:',
            # 检查rst链接格式 `link text <url>`_
            r'`[^`]+ <[^>]+>`_',
            # 检查行内代码 ``code``
            r'``[^`]+``',
            # 检查toctree指令
            r'\.\.\s+toctree\s*::',
            # 检查note等提示框指令
            r'\.\.\s+(note|warning|tip|important|caution)\s*::'
        ]

        # 如果包含任何一个reST语法模式，则返回True
        for pattern in patterns:
            if re.search(pattern, line):
                return True

        return False

    def _convert_headings(self, text: str) -> str:
        def repl(m: Match) -> str:
            return self._format_rst_heading(len(m.group(1)), m.group(2).strip())
        # 先处理标准的# 标题格式
        text = re.sub(r"^(#{1,6})\s+(.*)$", repl, text, flags=re.MULTILINE)
        # 再处理特殊的#. 标题格式
        text = re.sub(r"^(#)\.\s+(.*)$", repl, text, flags=re.MULTILINE)
        return text

    def _convert_images(self, text: str) -> str:
        def repl(match):
            alt_text = match.group(1)
            image_path = match.group(2)

            # 如果指定了源路径和目标路径，则调整图片相对路径
            if self.src_path and self.dst_path:
                # 判断是否为绝对路径
                if not os.path.isabs(image_path):
                    # 构建图片的绝对路径
                    absolute_image_path = os.path.join(self.src_path, image_path)
                    # 计算相对于目标路径的相对路径
                    relative_image_path = os.path.relpath(absolute_image_path, self.dst_path)
                    # 确保路径使用正斜杠（RST标准）
                    image_path = relative_image_path.replace(os.sep, '/')

            return f".. image:: {image_path}\n   :alt: {alt_text}"

        return re.sub(r'!\[(.*?)\]\((.*?)\)', repl, text)

    def _convert_links(self, text: str) -> str:
        text = re.sub(r'\[(.*?)\]\((.*?)\)', r'`\1 <\2>`_', text)
        # 参考：`link`_ 中冒号紧贴反引号会触发 literal 解析错误
        text = re.sub(r'([：:])(`[^`]+ <)', r'\1 \2', text)
        return text

    def _convert_bold(self, text: str) -> str:
        # Markdown **bold** -> RST :strong:`text`（避免与列表项 * 及中文括号冲突）
        return re.sub(r'(\*\*|__)(.+?)\1', r':strong:`\2`', text)

    def _convert_italic(self, text: str) -> str:
        if ':strong:' in text:
            return text
        """
        *italic* 或 _italic_ -> *italic*
        避免误伤普通下划线，如 link_to
        """
        # 只匹配被空格或行首行尾包围的 _xxx_
        text = re.sub(r'(?<!\w)\*(?!\*)(.+?)(?<!\*)\*(?!\w)', r'*\1*', text)
        text = re.sub(r'(?<!\w)_(?!_)(.+?)(?<!_)_(?!\w)', r'*\1*', text)
        return text

    def _convert_codeblocks(self, text: str) -> str:
        """转换Markdown代码块为reST代码块"""
        def repl(m: Match) -> str:
            lang = m.group(1).strip() if m.group(1) else ""
            code = m.group(2)
            rst = f".. code-block:: {lang}\n\n"
            for line in code.splitlines():
                # 确保代码行正确缩进，避免reST语法错误
                rst += f"   {line}\n"
            return rst + "\n"

        # 使用更健壮的正则表达式匹配代码块
        # 使用DOTALL标志使.匹配换行符，使用贪婪模式确保完整匹配代码块
        return re.sub(r"```(\w*)\n(.*?)```", repl, text, flags=re.DOTALL)

    def _clean_text_before_conversion(self, text: str) -> str:
        """在转换前清理文本，移除可能导致问题的内容"""
        # 移除编辑器相关的标记或注释
        text = re.sub(r'用户\d+\s+复制\s+删除\s+', '', text)
        # README 参考工程表同步锚点仅供脚本使用，不进 RST/HTML（否则会成段或露字）
        text = re.sub(r'<!--\s*readme-projects:(?:begin|end)\s*-->', '', text)
        # 移除多余的空行
        text = re.sub(r'\n{3,}', '\n\n', text)
        return text

    def _convert_inline_code(self, text: str) -> str:
        links = []

        def stash_link(m: Match) -> str:
            links.append(m.group(0))
            return f'\x00RSTLINK{len(links) - 1}\x00'

        text = re.sub(r'`[^`]+ <[^>]+>`_', stash_link, text)

        def repl(m: Match) -> str:
            start = m.start()
            if re.search(r':(?:strong|literal|emphasis|link_to_translation):$', text[:start]):
                return m.group(0)
            inner = m.group(1)
            if re.match(r'^(en|zh_CN):\[', inner):
                return m.group(0)
            stripped = re.sub(r'^\*\*+|\*\*+$', '', inner)
            if stripped != inner:
                return f'``{stripped}``'
            return f'``{inner}``'

        text = re.sub(r'`([^`]+)`', repl, text)
        for idx, link in enumerate(links):
            text = text.replace(f'\x00RSTLINK{idx}\x00', link)
        return text

    def _fix_unbalanced_strong(self, text: str) -> str:
        """Remove orphan ** markers left from malformed README emphasis."""
        while True:
            stars = [m.start() for m in re.finditer(r'\*\*', text)]
            if len(stars) % 2 == 0:
                break
            text = text[:stars[-1]] + text[stars[-1] + 2:]
        return text

    def _convert_unordered_list(self, text: str) -> str:
        return re.sub(r'^[\-\*]\s+', r'- ', text, flags=re.MULTILINE)

    def _convert_ordered_list(self, text: str) -> str:
        # 保持原始数字格式，不转换为#.格式
        return text

def translate_md2rst(src_path, dst_path, lan):
    src_file = ""
    dst_file = ""

    if lan == 'en':
        src_file = "README.md"
        dst_file = "index.rst"

    elif lan == 'zh_CN':
        src_file = "README_CN.md"
        dst_file = "index.rst"
    else:
        return

    # 检查源文件是否存在
    if not os.path.isfile(os.path.join(src_path, src_file)):
        return

    # 使用类的方法进行文件转换
    converter = MarkdownToRST(src_path, dst_path)
    converter.convert_file(src_file, dst_file)


# 快速入门按 README 固定标签抽四节；概述 / 文档 / 硬件 / 参考工程等留在仓库 README。
HOMEPAGE_H2 = {
	"zh_CN": (
		"获取代码",
		"编译环境安装",
		"编译工程",
		"烧录固件",
	),
	"en": (
		"Get the Code",
		"Set Up the Build Environment",
		"Build the Project",
		"Flash the Firmware",
	),
}


# README H2 上一行：<!-- readme-get-started:code|env|build|flash -->
HOMEPAGE_SECTIONS = (
	("code", "quick-start"),
	("env", "setup-env"),
	("build", "build-project"),
	("flash", "flash-firmware"),
)

_README_GET_STARTED_TAG = re.compile(
	r"^<!--\s*readme-get-started:(code|env|build|flash)\s*-->\s*$"
)


def _h2_title_from_md(chunk):
	m = re.search(r"^## (.+?)\s*$", chunk, re.M)
	return m.group(1).strip() if m else None


def extract_readme_homepage_sections(md_text, lan):
	"""按固定标签抽出四节，返回 (tag, rst_id, md_chunk)，顺序固定。"""
	lines = md_text.splitlines(keepends=True)
	fallback_title_to_tag = {}
	for i, title in enumerate(HOMEPAGE_H2.get(lan, ())):
		fallback_title_to_tag[title] = HOMEPAGE_SECTIONS[i][0]

	tagged = {}
	fallback = {}
	i = 0
	n = len(lines)
	while i < n and not lines[i].startswith("## "):
		i += 1
	while i < n:
		m = re.match(r"^## (.+?)\s*$", lines[i])
		title = m.group(1).strip() if m else ""
		tag = None
		j = i - 1
		while j >= 0 and not lines[j].strip():
			j -= 1
		if j >= 0:
			cm = _README_GET_STARTED_TAG.match(lines[j].strip())
			if cm:
				tag = cm.group(1)
		start = i
		i += 1
		while i < n and not lines[i].startswith("## "):
			i += 1
		chunk = "".join(
			ln for ln in lines[start:i]
			if not _README_GET_STARTED_TAG.match(ln.strip())
		)
		if tag:
			tagged[tag] = chunk
		elif title in fallback_title_to_tag:
			fallback[fallback_title_to_tag[title]] = chunk

	source = dict(fallback)
	source.update(tagged)
	out = []
	missing = []
	for tag, rst_id in HOMEPAGE_SECTIONS:
		chunk = source.get(tag)
		if chunk:
			out.append((tag, rst_id, chunk))
		else:
			missing.append(tag)
	if missing:
		print(f"extract_readme_homepage: missing {missing} ({lan})")
	return out


def _inject_homepage_rst_labels(rst, sections):
	"""按抽到的真实标题写 RST 锚点，标题改了也能对上。"""
	for _tag, rst_id, chunk in sections:
		title = _h2_title_from_md(chunk)
		if not title:
			continue
		rst = re.sub(
			rf"^{re.escape(title)}\n([=\-~`:'\"^+*#<>]{{3,}})",
			rf".. _{rst_id}:\n\n{title}\n\1",
			rst,
			count=1,
			flags=re.MULTILINE,
		)
	return rst



SMP_GET_STARTED_LINK_IDS = (
	"smp-get-started-env",
	"smp-get-started-flash",
)


def _heading_after_rst_label(rst_text, label):
	m = re.search(
		rf"\.\. _{re.escape(label)}:\s*\n(?:\.\. _[\w.-]+:\s*\n)*\s*\n*([^\n]+)",
		rst_text,
	)
	if not m:
		return None
	title = m.group(1).strip()
	if not title or title.startswith("..") or set(title) <= set("=-~`:'\"^+*#<> "):
		return None
	return title


def find_smp_get_started_rst(chip_docs_path, lan):
	chip = os.path.basename(os.path.normpath(chip_docs_path))
	docs_dir = os.path.normpath(os.path.join(chip_docs_path, ".."))
	repo_root = os.path.dirname(docs_dir)
	local = os.path.join(chip_docs_path, lan, "get-started", "index.rst")
	if "bk_avdk_smp" in repo_root.replace("\\", "/") and os.path.isfile(local):
		return local
	m = re.search(r"release_(\d+\.\d+\.\d+)", repo_root)
	if not m:
		return None
	ver = m.group(1)
	parent = os.path.dirname(repo_root)
	if os.path.basename(parent) == "docs_solution":
		parent = os.path.dirname(parent)
	smp = os.path.join(
		parent, f"bk_avdk_smp_release_{ver}", "docs", chip, lan, "get-started", "index.rst"
	)
	return smp if os.path.isfile(smp) else None


def load_smp_get_started_titles(chip_docs_path, lan):
	path = find_smp_get_started_rst(chip_docs_path, lan)
	if not path:
		print(f"load_smp_get_started_titles: SMP get-started rst not found ({lan})")
		return {}
	with open(path, encoding="utf-8") as f:
		text = f.read()
	out = {}
	for label in SMP_GET_STARTED_LINK_IDS:
		title = _heading_after_rst_label(text, label)
		if title:
			out[label] = title
	return out


def rewrite_smp_deep_link_text(text, titles, lan):
	if not titles:
		return text

	def md_repl(m):
		label = m.group(2)
		title = titles.get(label)
		if not title:
			return m.group(0)
		return f"[{title}]({m.group(1)}#{label})"

	text = re.sub(
		r"\[(?:Armino SMP (?:快速入门：|Quick Start: ))?[^\]]*\]\((https://docs\.bekencorp\.com/arminodoc/bk_avdk_smp/[^)\s]+)#(smp-get-started-(?:env|flash))\)",
		md_repl,
		text,
	)

	def rst_repl(m):
		label = m.group(2)
		title = titles.get(label)
		if not title:
			return m.group(0)
		return f"`{title} <{m.group(1)}#{label}>`_"

	text = re.sub(
		r"`(?:Armino SMP (?:快速入门：|Quick Start: ))?[^`<]+ <(https://docs\.bekencorp\.com/arminodoc/bk_avdk_smp/[^>#\s]+)#(smp-get-started-(?:env|flash))>`_",
		rst_repl,
		text,
	)
	return text


def _sync_smp_deep_link_sources(src_path, src_file, dst_dir, chip_docs_path, lan):
	titles = load_smp_get_started_titles(chip_docs_path, lan)
	with open(src_path, "r", encoding="utf-8") as f:
		raw_md = f.read()
	new_md = rewrite_smp_deep_link_text(raw_md, titles, lan)
	if new_md != raw_md:
		with open(src_path, "w", encoding="utf-8") as f:
			f.write(new_md)
		print(f"rewrite_smp_deep_link_text: {src_file}")
	leftover = os.path.join(dst_dir, "get-started", "index.rst")
	if os.path.isfile(leftover):
		with open(leftover, "r", encoding="utf-8") as f:
			raw_rst = f.read()
		new_rst = rewrite_smp_deep_link_text(raw_rst, titles, lan)
		if new_rst != raw_rst:
			with open(leftover, "w", encoding="utf-8") as f:
				f.write(new_rst)
			print(f"rewrite_smp_deep_link_text: {lan}/get-started/index.rst")
	return new_md


def generate_solution_homepage(chip_docs_path, lan):
	"""docs/README*.md → <lang>/_readme_body.rst，供快速入门 include。"""
	if lan not in ("en", "zh_CN"):
		return
	src_dir = os.path.normpath(os.path.join(chip_docs_path, ".."))
	dst_dir = os.path.join(chip_docs_path, lan)
	src_file = "README.md" if lan == "en" else "README_CN.md"
	src_path = os.path.join(src_dir, src_file)
	if not os.path.isfile(src_path):
		return
	os.makedirs(dst_dir, exist_ok=True)
	raw_md = _sync_smp_deep_link_sources(src_path, src_file, dst_dir, chip_docs_path, lan)
	sections = extract_readme_homepage_sections(raw_md, lan)
	md_text = "".join(c for _, _, c in sections)
	converter = MarkdownToRST(src_dir, dst_dir)
	rst = converter.convert(md_text)
	rst = re.sub(r"\n{3,}", "\n\n", rst)
	rst = re.sub(r"\.\./projects/([^/\s>`]+)/?", r"projects/\1/index.html", rst)
	rst = _inject_homepage_rst_labels(rst, sections)
	# 快速入门侧栏用标题本身导航，不加 1. 2. 3.；三级标题保留为正式小节以便进侧栏。
	body_path = os.path.join(dst_dir, "_readme_body.rst")
	with open(body_path, "w", encoding="utf-8") as f:
		f.write(rst)
	print(f"generate_solution_homepage: {src_file} -> {body_path}")


def copy_projects_doc(src_path, dst_path, lan):
    print(f"copy_projects_doc: {src_path} -> {dst_path}")
    if not os.path.isdir(src_path):
        return 0

    has_doc = False
    has_cmakelist = 0

    # 检查当前文件夹是否为projects，如果是则设置has_doc = True
    if os.path.basename(src_path) == 'projects':
        has_doc = True

    for item in os.listdir(src_path):
        if item.startswith("."):
            continue
        item_path = os.path.join(src_path, item)
        if os.path.isfile(item_path):
            if item == "README.md" and lan == 'en':
                has_doc = True
            elif item == "README_CN.md" and lan == 'zh_CN':
                has_doc = True
            elif item == "projects.rst":
                has_doc = True

            if item == "CMakeLists.txt":
                has_cmakelist = 1

    if has_doc == False:
        return 0

    run_cmd(f'mkdir -p {dst_path}')
    translate_md2rst(src_path, dst_path, lan)

    if has_cmakelist == 1:
        return 1

    for item in os.listdir(src_path):
        if item.startswith("."):
            continue
        item_path = os.path.join(src_path, item)
        item_dst_path = os.path.join(dst_path, item)
        if os.path.isdir(item_path):
            has_cmakelist = has_cmakelist + copy_projects_doc(item_path, item_dst_path, lan)

    if (has_cmakelist == 0):
        run_cmd(f'rm -rf {dst_path}')

    return has_cmakelist


README_PROJECTS_BEGIN = "<!-- readme-projects:begin -->"
README_PROJECTS_END = "<!-- readme-projects:end -->"

SOLUTION_ONLINE_DOC = "bk_doorbell"


def _doc_version_path(version: str) -> str:
	ver = (version or "v4.0.1").strip()
	if not ver.startswith("v"):
		ver = "v" + ver
	return ver


def discover_reference_projects(projects_root: str):
	"""参考工程：任意层级目录，含 CMakeLists.txt + README_CN.md + README.md（支持 SMP 式嵌套）。"""
	if not os.path.isdir(projects_root):
		return []
	names = []
	for rel, _root, files in _iter_project_candidate_dirs(projects_root):
		if (
			"CMakeLists.txt" in files
			and "README_CN.md" in files
			and "README.md" in files
		):
			names.append(rel)
	return sorted(names)


_CMAKE_PROJECT_CMD = re.compile(r"^\s*project\s*\(", re.IGNORECASE)


def _cmake_has_project(cmake_path: str) -> bool:
	"""CMakeLists.txt 中是否有未注释的 project(...) 命令。"""
	try:
		with open(cmake_path, "r", encoding="utf-8", errors="replace") as f:
			for line in f:
				s = line.split("#", 1)[0].strip()
				if _CMAKE_PROJECT_CMD.match(s):
					return True
	except OSError:
		return False
	return False


def discover_cmake_projects(projects_root: str):
	"""方案 README 参考工程表：任意层级目录，含 CMakeLists.txt 且其中有 project(...)。"""
	if not os.path.isdir(projects_root):
		return []
	names = []
	for rel, root, files in _iter_project_candidate_dirs(projects_root):
		if "CMakeLists.txt" not in files:
			continue
		if _cmake_has_project(os.path.join(root, "CMakeLists.txt")):
			names.append(rel)
	return sorted(names)


def list_readme_table_projects(projects_root: str, soc: str = None):
	"""README 参考工程表：CMakeLists.txt 且含 project(...) 的目录。"""
	_ = soc
	return discover_cmake_projects(projects_root)


def _iter_project_candidate_dirs(projects_root: str):
	"""遍历 projects/ 下各级子目录（跳过 . 开头路径段）。"""
	for root, dirs, files in os.walk(projects_root):
		dirs[:] = sorted(d for d in dirs if not d.startswith("."))
		rel = os.path.relpath(root, projects_root)
		if rel == ".":
			continue
		yield rel.replace("\\", "/"), root, files


def _list_doc_project_paths(doc_projects_path: str):
	"""docs/.../projects/ 下含 index.rst 的工程路径（相对 projects/，支持嵌套）。"""
	paths = set()
	if not os.path.isdir(doc_projects_path):
		return paths
	for root, dirs, files in os.walk(doc_projects_path):
		dirs[:] = [d for d in dirs if not d.startswith(".")]
		if root == doc_projects_path:
			continue
		if "index.rst" in files:
			rel = os.path.relpath(root, doc_projects_path).replace("\\", "/")
			paths.add(rel)
	return paths


def _readme_reference_h2(readme_name: str) -> str:
	return "参考工程" if readme_name == "README_CN.md" else "Reference Projects"


def _readme_projects_table_stub(readme_name: str) -> str:
	_ = readme_name
	return README_PROJECTS_BEGIN + "\n" + README_PROJECTS_END + "\n"



def _normalize_readme_projects_spacing(text: str) -> str:
	"""参考工程章节：标题与锚点、end 与后续正文之间不插入空行。"""
	for h2 in ("参考工程", "Reference Projects"):
		text = re.sub(
			rf"(^##\s+{re.escape(h2)}\s*\r?\n)\s*(?=<!-- readme-projects:begin -->)",
			r"\1",
			text,
			flags=re.MULTILINE,
		)
	text = re.sub(
		re.escape(README_PROJECTS_END) + r"\n+(?=\S)",
		README_PROJECTS_END + "\n",
		text,
	)
	return text


def _repair_readme_project_markers(text: str, readme_name: str):
	"""在 ## 参考工程 / Reference Projects 下插入锚点模板，供 sync 替换。"""
	h2 = _readme_reference_h2(readme_name)
	lines = text.splitlines(keepends=True)
	start = None
	for idx, line in enumerate(lines):
		if re.match(rf"^##\s+{re.escape(h2)}\s*$", line):
			start = idx
			break
	if start is None:
		return None
	end = len(lines)
	for idx in range(start + 1, len(lines)):
		if lines[idx].startswith("## "):
			end = idx
			break
	suffix = lines[end:]
	while suffix and not suffix[0].strip():
		suffix = suffix[1:]
	new_lines = lines[: start + 1] + [_readme_projects_table_stub(readme_name)] + suffix
	return _normalize_readme_projects_spacing("".join(new_lines))


def ensure_readme_project_markers(docs_root: str):
	"""保证 README 含参考工程锚点；缺失时在对应章节下自动插入（WARN），避免因此中断编 doc。"""
	errors = []
	for readme_name in ("README_CN.md", "README.md"):
		readme_path = os.path.join(docs_root, readme_name)
		if not os.path.isfile(readme_path):
			errors.append(f"缺少 {readme_path}")
			continue
		with open(readme_path, "r", encoding="utf-8") as f:
			text = f.read()
		if README_PROJECTS_BEGIN in text and README_PROJECTS_END in text:
			continue
		repaired = _repair_readme_project_markers(text, readme_name)
		if repaired is None:
			h2 = _readme_reference_h2(readme_name)
			errors.append(
				f"{readme_name} 无「## {h2}」章节，无法写入参考工程表；"
				f"请增加该章节或手动加入 {README_PROJECTS_BEGIN} / {README_PROJECTS_END}"
			)
			continue
		with open(readme_path, "w", encoding="utf-8") as f:
			f.write(repaired)
		print(
			f"{PRINT_WARN}WARNING: {readme_name} 缺少参考工程锚点，"
			f"已在「## {_readme_reference_h2(readme_name)}」下自动插入模板{PRINT_RESET}"
		)
	return errors


def validate_reference_projects(projects_root: str, chip_docs_path: str):
	"""校验源码 projects/ 参考工程收录条件（失败则中断 make doc）。"""
	_ = chip_docs_path
	errors = []

	if not os.path.isdir(projects_root):
		errors.append(f"projects 目录不存在: {projects_root}")
		return errors

	for rel, _root, files in _iter_project_candidate_dirs(projects_root):
		cmake = "CMakeLists.txt" in files
		cn = "README_CN.md" in files
		en = "README.md" in files
		if not cmake and not cn and not en:
			continue
		# AP/CP 等仅含 CMakeLists、无 README 的子目录不是参考工程文档根
		if cmake and not cn and not en:
			continue
		label = f"projects/{rel}"
		if not cmake:
			# 深层目录仅有 README 的说明页不当参考工程，也不报错
			if "/" not in rel:
				errors.append(f"{label}: 存在 README 但缺少 CMakeLists.txt，无法作为参考工程收录")
			continue
		if not cn:
			errors.append(f"{label}: 缺少 README_CN.md（参考工程需中英 README 成对）")
		if not en:
			errors.append(f"{label}: 缺少 README.md（参考工程需中英 README 成对）")

	return errors


def warn_stale_doc_project_dirs(chip_docs_path: str, project_names, phase: str):
	"""docs 相对源码扫描的差异：WARNING 告知，不中断编译。"""
	expected = set(project_names)
	lang_paths = {}
	for lan in ("zh_CN", "en"):
		doc_projects = os.path.join(chip_docs_path, lan, "projects")
		lang_paths[lan] = _list_doc_project_paths(doc_projects)

	for lan in ("zh_CN", "en"):
		for rel in sorted(lang_paths[lan] - expected):
			stale_path = os.path.join(chip_docs_path, lan, "projects", rel)
			print(
				f"{PRINT_WARN}WARNING [{phase}]: 文档目录不在源码扫描列表中 "
				f"（将删除或请人工核对）: {stale_path}/{PRINT_RESET}"
			)

	if lang_paths["zh_CN"] != lang_paths["en"]:
		only_zh = sorted(lang_paths["zh_CN"] - lang_paths["en"])
		only_en = sorted(lang_paths["en"] - lang_paths["zh_CN"])
		if only_zh:
			print(
				f"{PRINT_WARN}WARNING [{phase}]: docs/projects 中英文不一致: "
				f"仅 zh_CN 存在 {', '.join(only_zh)}{PRINT_RESET}"
			)
		if only_en:
			print(
				f"{PRINT_WARN}WARNING [{phase}]: docs/projects 中英文不一致: "
				f"仅 en 存在 {', '.join(only_en)}{PRINT_RESET}"
			)


def remove_stale_doc_project_dirs(chip_docs_path: str, project_names):
	"""删除 docs 中不在扫描列表的工程文档目录；删除前打 WARNING，不触发编译失败。"""
	expected = set(project_names)
	for lan in ("zh_CN", "en"):
		doc_projects = os.path.join(chip_docs_path, lan, "projects")
		stale = sorted(_list_doc_project_paths(doc_projects) - expected, key=len, reverse=True)
		for rel in stale:
			target = os.path.join(doc_projects, rel)
			print(
				f"{PRINT_WARN}WARNING: 删除文档残留目录: {target}/{PRINT_RESET}"
			)
			shutil.rmtree(target, ignore_errors=False)


def _write_projects_index_rst(index_path: str, lan: str, project_names):
	# 按扫描结果写 toctree 条目；不用 :glob:，避免磁盘上未删干净的残留目录仍被收进侧栏。
	if lan == "zh_CN":
		body = (
			"参考工程\n"
			"===========================\n\n"
			":link_to_translation:`en:[English]`\n\n"
			".. toctree::\n"
			"    :maxdepth: 1\n\n"
		)
	else:
		body = (
			"Reference Projects\n"
			"===========================\n\n"
			":link_to_translation:`zh_CN:[中文]`\n\n"
			".. toctree::\n"
			"    :maxdepth: 1\n\n"
		)
	for name in project_names:
		body += f"    {name}/index\n"
	os.makedirs(os.path.dirname(index_path), exist_ok=True)
	with open(index_path, "w", encoding="utf-8") as f:
		f.write(body)
	print(f"write_projects_index_rst: {len(project_names)} projects -> {index_path}")


def prepare_reference_projects_for_doc_build(
	projects_root: str,
	chip_docs_path: str,
	docs_root: str,
	version: str,
	soc: str,
):
	"""校验通过后：同步 projects 文档树、index.rst、方案 README 参考工程表。"""
	marker_errors = ensure_readme_project_markers(docs_root)
	if marker_errors:
		fail_reference_projects_validation(marker_errors)

	project_names = discover_reference_projects(projects_root)
	warn_stale_doc_project_dirs(chip_docs_path, project_names, "before cleanup")
	remove_stale_doc_project_dirs(chip_docs_path, project_names)
	for lan in ("zh_CN", "en"):
		dst = os.path.join(chip_docs_path, lan, "projects")
		copy_projects_doc(projects_root, dst, lan)
		index_path = os.path.join(dst, "index.rst")
		_write_projects_index_rst(index_path, lan, project_names)
	sync_readme_project_table(docs_root, version, soc)
	warn_stale_doc_project_dirs(chip_docs_path, project_names, "after sync")
	print(
		f"prepare_reference_projects_for_doc_build: "
		f"{len(project_names)} projects synced: {', '.join(project_names) or '(none)'}"
	)


def fail_reference_projects_validation(errors):
	log_error("### Reference projects validation failed ###")
	for msg in errors:
		log_error(f"  - {msg}")
	sys.exit(-1)


def _project_display_name(rel_path: str) -> str:
	"""表内链接文字：去掉一级分类前缀（wifi/p2p → p2p）。"""
	if "/" not in rel_path:
		return rel_path
	return rel_path.split("/", 1)[1]


def _project_repo_href_and_label(name: str, projects_root: str, readme_file: str):
	"""返回 (显示名, 仓库内工程目录 href)。"""
	_ = projects_root, readme_file
	label = _project_display_name(name)
	href = f"/projects/{name}/"
	return label, href


def _render_readme_projects_table(
	projects, projects_root: str, soc: str, version: str, lan: str
) -> str:
	"""方案仓：每个工程一行链接列表（不用表格，避免拉宽/拉长）。"""
	_ = soc, version
	readme_file = "README_CN.md" if lan == "zh_CN" else "README.md"
	lines = []
	for name in projects:
		label, href = _project_repo_href_and_label(name, projects_root, readme_file)
		lines.append(f"- [{label}]({href})")
	return "\n".join(lines) if lines else ""



def _replace_readme_projects_block(readme_text: str, table_body: str) -> str:
	pattern = re.compile(
		re.escape(README_PROJECTS_BEGIN) + r".*?" + re.escape(README_PROJECTS_END),
		re.DOTALL,
	)
	replacement = README_PROJECTS_BEGIN + "\n" + table_body + "\n" + README_PROJECTS_END
	if not pattern.search(readme_text):
		raise ValueError(
			f"README missing markers {README_PROJECTS_BEGIN} / {README_PROJECTS_END}"
		)
	return pattern.sub(replacement, readme_text, count=1)


def sync_readme_project_table(docs_path: str, version: str, soc: str):
	"""扫描 projects/，更新 docs/README.md、docs/README_CN.md 参考工程表。"""
	projects_root = os.path.normpath(os.path.join(docs_path, "..", "projects"))
	project_names = list_readme_table_projects(projects_root, soc)
	for lan, readme_name in (("zh_CN", "README_CN.md"), ("en", "README.md")):
		table = _render_readme_projects_table(
			project_names, projects_root, soc, version, lan
		)
		readme_path = os.path.join(docs_path, readme_name)
		with open(readme_path, "r", encoding="utf-8") as f:
			text = f.read()
		text = _replace_readme_projects_block(text, table)
		text = _normalize_readme_projects_spacing(text)
		with open(readme_path, "w", encoding="utf-8") as f:
			f.write(text)
		print(
			f"sync_readme_project_table: {readme_name} "
			f"({len(project_names)} projects) -> {readme_path}"
		)


def run_cmd(cmd):
	process = subprocess.Popen(cmd, shell=True)
	process.wait()

	return process

def log_error(log):
	print(PRINT_READ + log + PRINT_RESET)

def print_error_lines(file_path, error_log):
	ret = False

	try:
		with open(file_path, 'r') as file:
			for line in file:
				if error_log in line:
					if ret is False:
						log_error(f"Error found in file: {file}")
						ret = True

					log_error(line.strip())

	except FileNotFoundError:
		log_error("File not found!")
		ret = True
	except Exception as e:
		log_error("An error occurred: " + str(e))
		ret = True

	return ret


def latex_error_check(path):
	#print("check path: " + path)
	ret = False

	files = glob.glob(f"{path}/*.log")

	for file in files:
		ret = print_error_lines(file, "Error:")

	return ret


def build_armino_doc(source_path, dest_path, build_path, landir, version):
	print("found souce: " + source_path + " dest: " + dest_path + " build: " + build_path)
	command = "make -C " + source_path + " arminodocs -j32 " + "TARGET_DIR=" + dest_path + " TARGET_VERSION=" + version
	print("\t" + command)


	#clean before build
	run_cmd(f'rm -rf {build_path}/{landir}')
	run_cmd(f'rm -rf {source_path}/source')

	run_cmd(command)

	if latex_error_check(f'{source_path}/_build/latex') is True:
		log_error("### Build Docs Error, Exit ###")
		exit(-1)

	#copy
	run_cmd(f'cp -rf {dest_path} {build_path}/{landir}')
  
	#clean after build
	run_cmd(f'rm -rf {source_path}/xml')
	run_cmd(f'rm -rf {source_path}/xml_in')
	run_cmd(f'rm -rf {source_path}/man')
	run_cmd(f'rm -rf {source_path}/../__pycache__')
	run_cmd(f'rm -rf {dest_path}')
	run_cmd(f'rm -rf {dest_path} {build_path}/{landir}/inc')
	run_cmd(f'rm -rf {dest_path} {build_path}/{landir}/latex')

def build_html(source_path, dest_path, build_path, landir):
	print("found souce: " + source_path + " dest: " + dest_path + " build: " + build_path)
	command = "make -C " + source_path + " arminodocs -j32 " + "TARGET_DIR=" + dest_path
	print("\t" + command)


	#clean before build
	run_cmd(f'rm -rf {build_path}/{landir}')

	if run_cmd(command) is not True:
		log_error("### Build Docs Error, Exit ###")
		exit(-1)

	#copy
	run_cmd(f'cp -rf {dest_path} {build_path}/{landir}')
  
def build_pdf(source_path, dest_path, build_path, landir):
	print("found souce: " + source_path + " dest: " + dest_path + " build: " + build_path)
	command = "make -C " + source_path + " latexpdf " + "TARGET_DIR=" + dest_path
	print("\t" + command)


	#clean before build
	run_cmd(f'rm -rf {build_path}/{landir}')

	run_cmd(command)

	#copy
	run_cmd(f'cp -rf {dest_path} {build_path}/{landir}')

def build_doc(target, docs_path, build_path, version):
	print("build %s docs" % target)

	if "ARMINO_SOC" not in os.environ:
		raise RuntimeError("not get soc name")
	soc_name = os.getenv("ARMINO_SOC")

	projects_root = os.path.normpath(os.path.join(docs_path, "..", "..", "projects"))
	docs_root = os.path.normpath(os.path.join(docs_path, ".."))

	errors = validate_reference_projects(projects_root, docs_path)
	if errors:
		fail_reference_projects_validation(errors)

	prepare_reference_projects_for_doc_build(
		projects_root, docs_path, docs_root, version, soc_name
	)

	subdirectories = [d for d in os.listdir(docs_path) if os.path.isdir(os.path.join(docs_path, d))]
	white_list = {"en", "zh_CN"}
	target_dirs = [x for x in subdirectories if x in white_list]

	if not os.path.exists(build_path):
		run_cmd(f'mkdir -p {build_path}')

	for subdir in target_dirs:
		if target == 'build' :
			return

		generate_solution_homepage(docs_path, subdir)

		source_path = docs_path + "/" + subdir
		dest_path = docs_path + "/" + subdir + "/_build"
		build_armino_doc(source_path, dest_path, build_path, subdir, version)

def build_all(docs_path, build_path, version):
	print("build all docs")

	subdirectories = [d for d in os.listdir(docs_path) if os.path.isdir(os.path.join(docs_path, d))]
	black_list = {"common", ".git"}
	target_dirs = [x for x in subdirectories if x not in black_list]

	for subdir in target_dirs:
		print("found target: " + subdir)
		build_doc(subdir, docs_path + "/" + subdir, build_path + "/" + subdir, version)

def main(argv):
	parser = argparse.ArgumentParser()
	parser.add_argument('--clean', type=bool, default=False)
	parser.add_argument('--target', type=str, default="all")
	parser.add_argument('--type', type=str, default="html")
	parser.add_argument('--version', type=str, default="v4.0.1")
	parser.add_argument(
		'--sync-readme-projects',
		action='store_true',
		help='Update docs/README reference project tables from projects/ only',
	)
	args = parser.parse_args()

	root_path = os.getcwd()
	if 'ARMINO_SOC' in os.environ:
		soc_name = os.getenv('ARMINO_SOC')
	else:
		raise RuntimeError("not get soc name")
	build_path = root_path + f"/build/doc"
	if os.path.exists(build_path) == False:
		os.makedirs(build_path, exist_ok=True)

	if args.sync_readme_projects:
		chip_docs = os.path.join(root_path, soc_name)
		projects_root = os.path.normpath(os.path.join(root_path, "..", "projects"))
		errors = validate_reference_projects(projects_root, chip_docs)
		if errors:
			fail_reference_projects_validation(errors)
		prepare_reference_projects_for_doc_build(
			projects_root, chip_docs, root_path, args.version, soc_name
		)
		return

	if args.clean and args.target == "all":
		return

	run_cmd(f'cp {root_path}/version.json {build_path}/version.json')

	if args.clean == False and args.target == "all":
		build_all(root_path, build_path, args.version)
		return
 
	if os.path.exists(os.path.join(root_path, args.target)):
		target_build_path = os.path.join(build_path, args.target)
		build_doc(args.target, os.path.join(root_path, args.target), target_build_path, args.version)
	else:
		print("Error not found target: %s" % os.path.join(root_path, args.target))

if __name__ == "__main__":
	main(sys.argv)
