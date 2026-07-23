import { useState, useEffect } from 'react'

export interface ModelInfo {
  id: string              // provider name: "deepseek" | "ollama"
  name: string            // display name
  model: string           // actual model string
  supports_thinking: boolean
}

const MODELS_URL = 'http://localhost:8080/models'

export function useModels() {
  const [models, setModels] = useState<ModelInfo[]>([])
  const [selectedId, setSelectedId] = useState('deepseek')

  useEffect(() => {
    fetch(MODELS_URL)
      .then(res => res.json())
      .then((arr: ModelInfo[]) => {
        setModels(arr)
        if (arr.length > 0 && !arr.find(m => m.id === selectedId))
          setSelectedId(arr[0]!.id)
      })
      .catch(() => {
        // 后端不可用时使用默认值
        setModels([
          { id: 'deepseek', name: 'DeepSeek v4-pro', model: 'deepseek-v4-pro', supports_thinking: true },
          { id: 'ollama', name: 'Ollama (qwen2.5:3b)', model: 'qwen2.5:3b', supports_thinking: false },
        ])
      })
  }, [])  // eslint-disable-line react-hooks/exhaustive-deps

  return { models, selectedId, setSelectedId }
}
