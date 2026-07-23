import { type Components } from 'react-markdown'
import ReactMarkdown from 'react-markdown'
import remarkGfm from 'remark-gfm'
import rehypeHighlight from 'rehype-highlight'

// highlight.js 暗色主题（github-dark）
import 'highlight.js/styles/github-dark.css'

interface MarkdownRendererProps {
  content: string
}

/**
 * 自定义 Markdown 渲染组件。
 * - 代码块：GitHub 暗色风格语法高亮
 * - 表格 / 链接 / 列表等通过 remark-gfm 支持
 */
export default function MarkdownRenderer({ content }: MarkdownRendererProps) {
  return (
    <div className="markdown-body prose prose-invert prose-sm max-w-none text-gray-100 leading-relaxed">
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        rehypePlugins={[rehypeHighlight]}
        components={markdownComponents}
      >
        {content}
      </ReactMarkdown>
    </div>
  )
}

/** 自定义组件：内联代码 & 代码块样式 */
const markdownComponents: Partial<Components> = {
  // 内联代码
  code({ className, children, ...props }) {
    // react-markdown 对代码块给 className="language-xxx"，内联没有
    const isBlock = className?.startsWith('language-')

    if (isBlock) {
      return (
        <code className={className} {...props}>
          {children}
        </code>
      )
    }

    return (
      <code
        className="bg-gray-800 text-pink-400 rounded px-1.5 py-0.5 text-[0.85em] font-mono"
        {...props}
      >
        {children}
      </code>
    )
  },

  // 代码块外层 pre
  pre({ children }) {
    return (
      <pre className="bg-gray-900 border border-gray-700 rounded-xl p-4 overflow-x-auto text-sm my-3">
        {children}
      </pre>
    )
  },

  // 链接
  a({ children, href, ...props }) {
    return (
      <a
        href={href}
        target="_blank"
        rel="noopener noreferrer"
        className="text-blue-400 hover:text-blue-300 underline transition-colors"
        {...props}
      >
        {children}
      </a>
    )
  },

  // 表格
  table({ children }) {
    return (
      <div className="overflow-x-auto my-3">
        <table className="w-full border-collapse text-sm">{children}</table>
      </div>
    )
  },
  thead({ children }) {
    return <thead className="border-b-2 border-gray-600">{children}</thead>
  },
  th({ children }) {
    return <th className="px-3 py-2 text-left font-semibold text-gray-300">{children}</th>
  },
  td({ children }) {
    return <td className="px-3 py-2 border-b border-gray-700/50">{children}</td>
  },

  // 列表
  ul({ children }) {
    return <ul className="list-disc pl-5 my-2 space-y-1">{children}</ul>
  },
  ol({ children }) {
    return <ol className="list-decimal pl-5 my-2 space-y-1">{children}</ol>
  },
}
