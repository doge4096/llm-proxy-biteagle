interface ReasoningBlockProps {
  content: string
}

export default function ReasoningBlock({ content }: ReasoningBlockProps) {
  if (!content) return null

  return (
    <details open className="mb-3">
      <summary className="cursor-pointer text-xs font-medium text-gray-400 hover:text-gray-300 transition-colors select-none">
        🤔 思维链
      </summary>
      <div className="mt-2 p-3 rounded-lg bg-gray-800/60 border border-gray-700/50 text-xs text-gray-400 leading-relaxed whitespace-pre-wrap max-h-64 overflow-y-auto">
        {content}
      </div>
    </details>
  )
}
