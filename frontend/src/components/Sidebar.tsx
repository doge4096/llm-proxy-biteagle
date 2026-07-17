import type { Conversation } from '../hooks/useConversations'

interface SidebarProps {
  conversations: Conversation[]
  currentId: string
  onCreate: () => void
  onSwitch: (id: string) => void
  onDelete: (id: string) => void
}

/** 格式化时间戳为短格式 */
function formatTime(ts: number): string {
  const d = new Date(ts)
  const month = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  const hour = String(d.getHours()).padStart(2, '0')
  const min = String(d.getMinutes()).padStart(2, '0')
  return `${month}-${day} ${hour}:${min}`
}

export default function Sidebar({
  conversations,
  currentId,
  onCreate,
  onSwitch,
  onDelete,
}: SidebarProps) {
  // 按创建时间倒序排列
  const sorted = [...conversations].sort((a, b) => b.createdAt - a.createdAt)

  return (
    <aside className="w-64 shrink-0 h-screen flex flex-col border-r border-gray-800 bg-gray-900/60">
      {/* 新建对话按钮 */}
      <div className="p-3">
        <button
          onClick={onCreate}
          className="w-full rounded-lg border border-gray-700 hover:border-gray-500 bg-gray-800/60 hover:bg-gray-700/60 text-gray-200 text-sm py-2.5 px-4 transition-colors cursor-pointer flex items-center justify-center gap-2"
        >
          <span className="text-lg leading-none">+</span>
          新对话
        </button>
      </div>

      {/* 对话列表 */}
      <nav className="flex-1 overflow-y-auto px-2 pb-2 space-y-0.5">
        {sorted.map((conv) => {
          const isActive = conv.id === currentId
          const hasMessages = conv.messages.length > 0

          return (
            <div
              key={conv.id}
              onClick={() => onSwitch(conv.id)}
              className={`group relative rounded-lg px-3 py-2.5 cursor-pointer transition-colors ${
                isActive
                  ? 'bg-gray-700/80'
                  : 'hover:bg-gray-800/60'
              }`}
            >
              {/* 标题 + 删除按钮 */}
              <div className="flex items-center justify-between gap-2">
                <span
                  className={`text-sm truncate flex-1 ${
                    isActive ? 'text-gray-100' : 'text-gray-400'
                  }`}
                  title={conv.title}
                >
                  {hasMessages ? '💬 ' : '📝 '}
                  {conv.title}
                </span>

                {/* 删除按钮 — hover 时显示 */}
                <button
                  onClick={(e) => {
                    e.stopPropagation()
                    onDelete(conv.id)
                  }}
                  className="shrink-0 text-gray-600 hover:text-red-400 opacity-0 group-hover:opacity-100 transition-all cursor-pointer text-sm leading-none px-1"
                  title="删除对话"
                >
                  ×
                </button>
              </div>

              {/* 时间戳 */}
              <p className="text-xs text-gray-600 mt-1">
                {formatTime(conv.createdAt)}
              </p>
            </div>
          )
        })}
      </nav>
    </aside>
  )
}
